#include "IBLMap.h"
#include "ImmediateSubmitter.h"
#include "VulkanBuffer.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDescriptorPool.h"
#include "DescriptorWriter.h"
#include "VulkanPipeline.h"

IBLProcessor::IBLProcessor(VulkanContext& context, ImmediateSubmitter& uploader)
    : _context(context), _uploader(uploader) {
    createPipelines();
    generateBrdfMap();

}

void IBLProcessor::addSkybox(VulkanImage& skybox,std::string name) {
    IBTextures iblTextures;
    VkExtent2D cubemapExtent = { 32, 32 };
    iblTextures.irradianceMap = VulkanImage::createCube(
        _context, cubemapExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    generateIrradianceMap(skybox, *iblTextures.irradianceMap);
    VkExtent2D prefilteredExtent = { 512, 512 };
    uint32_t mipLevels = static_cast<uint32_t>(floor(log2(prefilteredExtent.width))) + 1;
    iblTextures.prefilteredMap = VulkanImage::createCube(
        _context, prefilteredExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, mipLevels
    );
    generatePrefilteredMap(skybox, *iblTextures.prefilteredMap);
    _ibTextures[name] = std::move(iblTextures);
}

IBLProcessor::~IBLProcessor() {
}

void IBLProcessor::createPipelines() {
    _brdfLutLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    _brdfLutPipeline = PipelineBuilder(_context)
        .buildComputePipeline("res\\IBLComputeShader\\brdf.spv", _brdfLutLayout->getLayout(),"CSMain");

    _irradianceLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    _irradiancePipeline = PipelineBuilder(_context)
        .buildComputePipeline("res\\IBLComputeShader\\irradiance.spv", _irradianceLayout->getLayout(),"CSMain");

    _prefilterLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // 为 Push Constant 定义范围
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float); // 我们将传递一个 float 类型的 roughness

    // 创建包含 Push Constant 的 PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[] = { _prefilterLayout->getLayout() };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    // 直接创建 Pipeline 和 Layout，因为 PipelineBuilder 可能不支持 Push Constants
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(_context.getDevice(), &pipelineLayoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create prefilter pipeline layout!");
    }

    auto computeShaderCode = _context.readFile("res\\IBLComputeShader\\prefilter_specular.spv");
    VkShaderModule computeShaderModule = _context.createShaderModule(computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "CSMain";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = layout;
    pipelineInfo.stage = computeShaderStageInfo;

    VkPipeline pipeline;
    if (vkCreateComputePipelines(_context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(_context.getDevice(), layout, nullptr);
        throw std::runtime_error("failed to create compute pipeline!");
    }

    _prefilterPipeline = std::make_unique<VulkanPipeline>(_context, pipeline, layout);

    vkDestroyShaderModule(_context.getDevice(), computeShaderModule, nullptr);
    _prefilterPool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    _prefilterSet = _prefilterPool->allocateSet(*_prefilterLayout);
}

void IBLProcessor::generateIrradianceMap(
    VulkanImage& environmentCubemap, // 输入: 原始 HDR 立方体图
    VulkanImage& irradianceMap       // 输出: 目标辐照度图
) {
    auto pool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    VkDescriptorSet descriptorSet = pool->allocateSet(*_irradianceLayout);

    VkDescriptorImageInfo envMapInfo = environmentCubemap.getDescriptorInfo();
	VkDescriptorImageInfo irradianceMapInfo = irradianceMap.getDescriptorInfoForStorage();

    DescriptorWriter(_context, descriptorSet)
        .writeImage(0, &envMapInfo)
        .writeImage(1, &irradianceMapInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .update();

    _uploader.submit([&](VkCommandBuffer cmd) {
        irradianceMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

        _irradiancePipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _irradiancePipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);

        uint32_t dim = irradianceMap.getExtent().width;
        vkCmdDispatch(cmd, dim / 32, dim / 32, 6);

        irradianceMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "[SUCCESS]Generate IrradianceMap" << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
}

void IBLProcessor::generateBrdfMap() {
    std::cout << "[INFO] Generating BRDF LUT..." << std::endl;

    // 1. 创建 BRDF LUT 图像资源
    _brdfLUT = VulkanImage::createImage2D(_context, { 512, 512 }, VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // 2. 创建一个临时的描述符池和集
    auto pool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    VkDescriptorSet descriptorSet = pool->allocateSet(*_brdfLutLayout);

    // 3. 更新描述符集，将 BRDF LUT 绑定为 storage image
    VkDescriptorImageInfo lutImageInfo = _brdfLUT->getDescriptorInfoForStorage();
    DescriptorWriter(_context, descriptorSet)
        .writeImage(0, &lutImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .update();

    // 4. 使用 uploader 提交计算任务
    _uploader.submit([&](VkCommandBuffer cmd) {
        // a. 将输出图像的布局转换为 GENERAL，以准备写入
        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

        // b. 绑定管线和描述符集
        _brdfLutPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _brdfLutPipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);

        // c. Dispatch 计算任务
        //    线程组的数量 = 纹理尺寸 / 局部线程组大小 (向上取整)
        uint32_t dim = _brdfLUT->getExtent().width;
        vkCmdDispatch(cmd, dim / 32, dim / 32, 1); // 2D 纹理，Z 维度是 1

        // d. 将输出图像的布局转换为 SHADER_READ_ONLY，以备 PBR 片段着色器采样
        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    std::cout << "[SUCCESS] BRDF LUT generated." << std::endl;
}

void IBLProcessor::generatePrefilteredMap(VulkanImage& environmentCubemap, VulkanImage& prefilteredMap) {
    std::cout << "[INFO] Generating Pre-filtered Specular Map..." << std::endl;

    uint32_t mipLevels = prefilteredMap.getMipLevels();
    if (mipLevels <= 1) {
        std::cout << "[WARN] Prefiltered map has only 1 mip level. Skipping generation." << std::endl;
        return;
    }

    // --- 准备一次性的描述符更新 ---
    // 绑定不会改变的输入环境图 (binding 0)
    VkDescriptorImageInfo envMapInfo = environmentCubemap.getDescriptorInfo();
    DescriptorWriter(_context, _prefilterSet)
        .writeImage(0, &envMapInfo) // combined image sampler
        .update();

    // --- 提交一个包含所有 Mip Level 计算的命令缓冲区 ---
    _uploader.submit([&](VkCommandBuffer cmd) {
        // 1. 将整个 prefilteredMap 转换为 GENERAL 布局，以准备接收所有 Mip 层的写入
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

        // 2. 绑定管线，因为整个循环都用同一个
        _prefilterPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

        // --- 循环遍历所有 Mipmap 等级 ---
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            // a. 为当前 Mip Level 创建一个专门的 ImageView
            //    这个视图只“看到” prefilteredMap 的第 mip 层
            VkImageView mipView = _context.createImageView(
                prefilteredMap.getImage(),
                prefilteredMap.getFormat(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_VIEW_TYPE_CUBE,
                6,           // layerCount: 我们要写入所有6个面
                0,           // baseArrayLayer
                1,           // levelCount: 这个视图只包含 1 个 mip level
                mip          // baseMipLevel: 从第 mip 个 level 开始
            );

            // b. 更新描述符集，让输出 (binding 1) 指向这个新的 Mip 视图
            VkDescriptorImageInfo storageImageInfo = {};
            storageImageInfo.imageView = mipView;
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            DescriptorWriter(_context, _prefilterSet)
                .writeImage(1, &storageImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .update();

            // c. 绑定更新后的描述符集
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _prefilterPipeline->getLayout(), 0, 1, &_prefilterSet, 0, nullptr);

            // d. 计算当前 Mip 对应的 roughness，并通过 Push Constant 传递给着色器
            float roughness = (float)mip / (float)(mipLevels - 1);
            vkCmdPushConstants(cmd, _prefilterPipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

            // e. Dispatch 计算任务，大小根据当前 Mip 的尺寸而定
            uint32_t mipWidth = prefilteredMap.getExtent().width >> mip;
            uint32_t mipHeight = prefilteredMap.getExtent().height >> mip;
            // 确保至少 dispatch 一个线程组
            vkCmdDispatch(cmd, std::max(1u, mipWidth / 32), std::max(1u, mipHeight / 32), 6);

            // f. 销毁临时的 Mip 视图
            vkDestroyImageView(_context.getDevice(), mipView, nullptr);

            // g. (可选但推荐) 在每次 dispatch 之间插入一个屏障
            //    确保前一个 mip level 的写入对后续操作（如果需要的话）可见
            //    对于这个独立的循环，也可以省略，在循环后加一个总的屏障
            VkMemoryBarrier memoryBarrier{};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
        }

        // 3. 所有 Mip 计算完毕后，将整个 prefilteredMap 转换为适合着色器采样的布局
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    std::cout << "[SUCCESS] Pre-filtered Specular Map generated." << std::endl;
}

VulkanImage* IBLProcessor::getIrradianceMap(const std::string& name) const {
    // 1. 使用 map::find() 来安全地查找 key
    //    map::operator[] 在 key 不存在时会创建一个新元素，这在 const 函数中是不允许的
    auto it = _ibTextures.find(name);

    // 2. 检查是否找到了对应的天空盒
    if (it != _ibTextures.end()) {
        // it->second 指向 map 中的 IBTextures 对象
        // .get() 从 unique_ptr 中获取原始指针
        return it->second.irradianceMap.get();
    }

    // 3. 如果没有找到，返回空指针
    return nullptr;
}

VulkanImage* IBLProcessor::getPrefilteredMap(const std::string& name) const {
    auto it = _ibTextures.find(name);

    if (it != _ibTextures.end()) {
        return it->second.prefilteredMap.get();
    }

    return nullptr;
}

VulkanImage* IBLProcessor::getBrdfLUT() const {
    // BRDF LUT 是一个单独的成员，直接返回它的原始指针
    return _brdfLUT.get();
}