#pragma once
#include "VulkanContext.h"
#include <string>
#include <vector>
#include <memory>

class VulkanPipeline {
public:
    VulkanPipeline(VulkanContext& context, VkPipeline pipeline, VkPipelineLayout layout);
    ~VulkanPipeline();

    // 禁止拷贝
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    void bind(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint);

    VkPipelineLayout getLayout() const { return _layout; }
    VkPipeline getPipeline() const { return _pipeline; }

private:
    VulkanContext& _context;
    VkPipeline _pipeline;
    VkPipelineLayout _layout;
};

// 使用建造者模式来创建管线
class PipelineBuilder {
public:
    PipelineBuilder(VulkanContext& context);

    // 图形管线配置
    PipelineBuilder& addShaderStage(VkShaderStageFlagBits stage, const std::string& shaderPath, const char* entryPoint = "main");
    PipelineBuilder& setVertexInputState(const VkPipelineVertexInputStateCreateInfo& info);
    PipelineBuilder& setInputAssemblyState(const VkPipelineInputAssemblyStateCreateInfo& info);
    PipelineBuilder& setRasterizationState(const VkPipelineRasterizationStateCreateInfo& info);
    PipelineBuilder& setMultisampleState(const VkPipelineMultisampleStateCreateInfo& info);
    PipelineBuilder& setColorBlendState(const VkPipelineColorBlendStateCreateInfo& info);
    PipelineBuilder& setDepthStencilState(const VkPipelineDepthStencilStateCreateInfo& info);
    // 注意：我们不再需要 setDynamicStates，因为默认值中包含了它
    PipelineBuilder& addDescriptorSetLayout(VkDescriptorSetLayout layout);
    PipelineBuilder& setRenderingFormats(VkFormat colorFormat, VkFormat depthFormat);

    std::unique_ptr<VulkanPipeline> buildGraphicsPipeline();

    // 计算管线配置
    std::unique_ptr<VulkanPipeline> buildComputePipeline(const std::string& shaderPath, VkDescriptorSetLayout layout, const char* entryPoint = "main");

    ~PipelineBuilder();
private:
    VulkanContext& _context;
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    std::vector<VkShaderModule> _shaderModules;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo{};
    VkPipelineInputAssemblyStateCreateInfo _inputAssemblyInfo{};
    VkPipelineRasterizationStateCreateInfo _rasterizationInfo{};
    VkPipelineMultisampleStateCreateInfo _multisampleInfo{};
    VkPipelineColorBlendAttachmentState _colorBlendAttachment{}; // <--- 新增，用于默认颜色混合
    VkPipelineColorBlendStateCreateInfo _colorBlendInfo{};
    VkPipelineDepthStencilStateCreateInfo _depthStencilInfo{};
    std::vector<VkDynamicState> _dynamicStates; // <--- 新增
    VkPipelineDynamicStateCreateInfo _dynamicStateInfo{};
    std::vector<VkDescriptorSetLayout> _descriptorSetLayouts;
    VkPipelineRenderingCreateInfo _renderingInfo{};
};