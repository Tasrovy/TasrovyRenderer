#include "VulkanPipeline.h"
#include <stdexcept>
#include <vulkan/vulkan.h>

// --- VulkanPipeline implementation (无变化) ---
VulkanPipeline::VulkanPipeline(VulkanContext& context, VkPipeline pipeline, VkPipelineLayout layout)
    : _context(context), _pipeline(pipeline), _layout(layout) {}

VulkanPipeline::~VulkanPipeline() {
    vkDestroyPipeline(_context.getDevice(), _pipeline, nullptr);
    vkDestroyPipelineLayout(_context.getDevice(), _layout, nullptr);
}

void VulkanPipeline::bind(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint) {
    vkCmdBindPipeline(commandBuffer, bindPoint, _pipeline);
}

// --- PipelineBuilder implementation (已修改) ---
PipelineBuilder::PipelineBuilder(VulkanContext& context) : _context(context) {
    // --- 设置一套完整的、合理的图形管线默认值 ---

    // 顶点输入：默认为空，必须由用户提供
    _vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // 输入汇编：默认绘制三角形列表
    _inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    _inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    _inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

    // 光栅化：默认实心填充，逆时针正面，无背面剔除
    _rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    _rasterizationInfo.depthClampEnable = VK_FALSE;
    _rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
    _rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
    _rasterizationInfo.lineWidth = 1.0f;
    _rasterizationInfo.cullMode = VK_CULL_MODE_NONE; // 通常会设为 BACK_BIT
    _rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    _rasterizationInfo.depthBiasEnable = VK_FALSE;

    // 多重采样：默认使用 context 中 MSAA 等级，且不启用样本着色
    _multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    _multisampleInfo.sampleShadingEnable = VK_FALSE;
    _multisampleInfo.rasterizationSamples = context.getMsaaSamples();
    
    // 颜色混合：默认关闭颜色混合，直接覆盖颜色
    _colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    _colorBlendAttachment.blendEnable = VK_FALSE;
    _colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    _colorBlendInfo.logicOpEnable = VK_FALSE;
    _colorBlendInfo.attachmentCount = 1;
    _colorBlendInfo.pAttachments = &_colorBlendAttachment;

    // 深度/模板：默认开启深度测试和写入，使用 LESS 比较
    _depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    _depthStencilInfo.depthTestEnable = VK_TRUE;
    _depthStencilInfo.depthWriteEnable = VK_TRUE;
    _depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    _depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
    _depthStencilInfo.stencilTestEnable = VK_FALSE;

    // 动态状态：默认视口和裁剪是动态的
    _dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    _dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    _dynamicStateInfo.pDynamicStates = _dynamicStates.data();
    _dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(_dynamicStates.size());

    // 渲染信息：默认为空，必须由用户提供
    _renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
}

// --- 用户覆盖函数 ---
PipelineBuilder& PipelineBuilder::addShaderStage(VkShaderStageFlagBits stage, const std::string& shaderPath, const char* entryPoint) {
    auto shaderCode = _context.readFile(shaderPath);
    VkShaderModule shaderModule = _context.createShaderModule(shaderCode);
    _shaderModules.push_back(shaderModule);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = stage;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = entryPoint;
    _shaderStages.push_back(shaderStageInfo);
    return *this;
}

PipelineBuilder& PipelineBuilder::setVertexInputState(const VkPipelineVertexInputStateCreateInfo& info) {
    _vertexInputInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::setInputAssemblyState(const VkPipelineInputAssemblyStateCreateInfo& info) {
    _inputAssemblyInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::setRasterizationState(const VkPipelineRasterizationStateCreateInfo& info) {
    _rasterizationInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::setMultisampleState(const VkPipelineMultisampleStateCreateInfo& info) {
    _multisampleInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::setColorBlendState(const VkPipelineColorBlendStateCreateInfo& info) {
    _colorBlendInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthStencilState(const VkPipelineDepthStencilStateCreateInfo& info) {
    _depthStencilInfo = info;
    return *this;
}

PipelineBuilder& PipelineBuilder::addDescriptorSetLayout(VkDescriptorSetLayout layout) {
    _descriptorSetLayouts.push_back(layout);
    return *this;
}

PipelineBuilder& PipelineBuilder::setRenderingFormats(VkFormat colorFormat, VkFormat depthFormat) {
    // 这里需要特别注意，因为pColorAttachmentFormats需要一个持久的指针
    // 我们将colorFormat存储在_renderingInfo的一个隐藏成员中
    static VkFormat staticColorFormat; // 使用静态变量以确保指针有效
    staticColorFormat = colorFormat;
    _renderingInfo.colorAttachmentCount = 1;
    _renderingInfo.pColorAttachmentFormats = &staticColorFormat;
    _renderingInfo.depthAttachmentFormat = depthFormat;
    return *this;
}

// --- 构建函数 ---

std::unique_ptr<VulkanPipeline> PipelineBuilder::buildGraphicsPipeline() {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(_descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = _descriptorSetLayouts.data();

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(_context.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &_renderingInfo; // 链接动态渲染信息
    pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
    pipelineInfo.pStages = _shaderStages.data();
    pipelineInfo.pVertexInputState = &_vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &_inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &_rasterizationInfo;
    pipelineInfo.pMultisampleState = &_multisampleInfo;
    pipelineInfo.pColorBlendState = &_colorBlendInfo;
    pipelineInfo.pDepthStencilState = &_depthStencilInfo;
    pipelineInfo.pDynamicState = &_dynamicStateInfo;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // 动态渲染不需要Render Pass
    
    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(_context.getDevice(), pipelineLayout, nullptr);
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    // 清理临时创建的shader modules
    for (auto module : _shaderModules) {
        vkDestroyShaderModule(_context.getDevice(), module, nullptr);
    }
    _shaderModules.clear();

    return std::make_unique<VulkanPipeline>(_context, pipeline, pipelineLayout);
}

std::unique_ptr<VulkanPipeline> PipelineBuilder::buildComputePipeline(const std::string& shaderPath, VkDescriptorSetLayout layout, const char* entryPoint) {
    auto computeShaderCode = _context.readFile(shaderPath);
    VkShaderModule computeShaderModule = _context.createShaderModule(computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = entryPoint;
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(_context.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = computeShaderStageInfo;
    
    VkPipeline pipeline;
    if (vkCreateComputePipelines(_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(_context.getDevice(), pipelineLayout, nullptr);
        throw std::runtime_error("failed to create compute pipeline!");
    }

    vkDestroyShaderModule(_context.getDevice(), computeShaderModule, nullptr);

    return std::make_unique<VulkanPipeline>(_context, pipeline, pipelineLayout);
}

PipelineBuilder::~PipelineBuilder() {
    if (!_shaderModules.empty()) {
        for (auto module : _shaderModules) {
            vkDestroyShaderModule(_context.getDevice(), module, nullptr);
        }
    }
}