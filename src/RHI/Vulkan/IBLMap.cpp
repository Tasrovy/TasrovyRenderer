#include "IBLMap.h"

#include "DescriptorWriter.h"
#include "ImmediateSubmitter.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanImage.h"
#include "VulkanPipeline.h"
#include <Logger.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

uint32_t ceilDiv(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

std::unique_ptr<VulkanPipeline> createComputePipelineWithPushConstants(
    VulkanContext& context,
    const std::string& shaderPath,
    VkDescriptorSetLayout descriptorSetLayout,
    VkShaderStageFlags pushStages,
    uint32_t pushSize,
    const char* entryPoint) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = pushStages;
    pushConstantRange.offset = 0;
    pushConstantRange.size = pushSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(context.getDevice(), &pipelineLayoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline layout with push constants!");
    }

    auto computeShaderCode = context.readFile(shaderPath);
    VkShaderModule computeShaderModule = context.createShaderModule(computeShaderCode);

    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = entryPoint;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = layout;
    pipelineInfo.stage = computeShaderStageInfo;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(context.getDevice(), computeShaderModule, nullptr);
        vkDestroyPipelineLayout(context.getDevice(), layout, nullptr);
        throw std::runtime_error("failed to create compute pipeline!");
    }

    vkDestroyShaderModule(context.getDevice(), computeShaderModule, nullptr);
    return std::make_unique<VulkanPipeline>(context, pipeline, layout);
}

} // namespace

IBLProcessor::IBLProcessor(VulkanContext& context, ImmediateSubmitter& uploader)
    : _context(context), _uploader(uploader) {
    createPipelines();
    generateBrdfMap();
}

IBLProcessor::~IBLProcessor() = default;

void IBLProcessor::createPipelines() {
    _brdfLutLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();
    _brdfLutPipeline = PipelineBuilder(_context)
        .buildComputePipeline("res\\IBLComputeShader\\brdf.spv", _brdfLutLayout->getLayout(), "CSMain");

    _irradianceLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();
    _irradiancePipeline = PipelineBuilder(_context)
        .buildComputePipeline("res\\IBLComputeShader\\irradiance.spv", _irradianceLayout->getLayout(), "CSMain");

    _prefilterLayout = VulkanDescriptorSetLayout::Builder(_context)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();
    _prefilterPipeline = createComputePipelineWithPushConstants(
        _context,
        "res\\IBLComputeShader\\prefilter_specular.spv",
        _prefilterLayout->getLayout(),
        VK_SHADER_STAGE_COMPUTE_BIT,
        sizeof(float),
        "CSMain");
}

void IBLProcessor::addSkybox(VulkanImage& skybox, std::string name) {
    if (_ibTextures.contains(name)) {
        return;
    }

    IBTextures iblTextures;

    iblTextures.irradianceMap = VulkanImage::createCube(
        _context,
        VkExtent2D{32, 32},
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    generateIrradianceMap(skybox, *iblTextures.irradianceMap);

    const VkExtent2D prefilteredExtent{128, 128};
    const uint32_t mipLevels =
        static_cast<uint32_t>(std::floor(std::log2(prefilteredExtent.width))) + 1;
    iblTextures.prefilteredMap = VulkanImage::createCube(
        _context,
        prefilteredExtent,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        mipLevels);
    generatePrefilteredMap(skybox, *iblTextures.prefilteredMap);

    _ibTextures[std::move(name)] = std::move(iblTextures);
}

void IBLProcessor::generateBrdfMap() {
    LOG_INFO("IBL: generating BRDF LUT");

    _brdfLUT = VulkanImage::createImage2D(
        _context,
        VkExtent2D{256, 256},
        VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    auto pool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    VkDescriptorSet descriptorSet = pool->allocateSet(*_brdfLutLayout);
    VkDescriptorImageInfo lutImageInfo = _brdfLUT->getDescriptorInfoForStorage();
    DescriptorWriter(_context, descriptorSet)
        .writeImage(0, &lutImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .update();

    _uploader.submit([&](VkCommandBuffer cmd) {
        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
        _brdfLutPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            _brdfLutPipeline->getLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr);

        const auto extent = _brdfLUT->getExtent();
        vkCmdDispatch(cmd, ceilDiv(extent.width, 32), ceilDiv(extent.height, 32), 1);

        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    LOG_INFO("IBL: BRDF LUT generated");
}

void IBLProcessor::generateIrradianceMap(VulkanImage& environmentCubemap, VulkanImage& irradianceMap) {
    LOG_INFO("IBL: generating irradiance cubemap");

    auto pool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    VkDescriptorSet descriptorSet = pool->allocateSet(*_irradianceLayout);
    VkImageView storageView = _context.createImageView(
        irradianceMap.getImage(),
        irradianceMap.getFormat(),
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        6,
        0,
        1,
        0);

    VkDescriptorImageInfo envMapInfo = environmentCubemap.getDescriptorInfo();
    VkDescriptorImageInfo irradianceMapInfo{};
    irradianceMapInfo.imageView = storageView;
    irradianceMapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    DescriptorWriter(_context, descriptorSet)
        .writeImage(0, &envMapInfo)
        .writeImage(1, &irradianceMapInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .update();

    _uploader.submit([&](VkCommandBuffer cmd) {
        irradianceMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
        _irradiancePipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            _irradiancePipeline->getLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr);

        const auto extent = irradianceMap.getExtent();
        vkCmdDispatch(cmd, ceilDiv(extent.width, 32), ceilDiv(extent.height, 32), 6);

        irradianceMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    vkDestroyImageView(_context.getDevice(), storageView, nullptr);
    LOG_INFO("IBL: irradiance cubemap generated");
}

void IBLProcessor::generatePrefilteredMap(VulkanImage& environmentCubemap, VulkanImage& prefilteredMap) {
    LOG_INFO("IBL: generating prefiltered specular cubemap");

    const uint32_t mipLevels = prefilteredMap.getMipLevels();
    if (mipLevels <= 1) {
        LOG_WARN("IBL: prefiltered map has only one mip level");
        return;
    }

    _uploader.submit([&](VkCommandBuffer cmd) {
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);
    });

    VkDescriptorImageInfo envMapInfo = environmentCubemap.getDescriptorInfo();

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        auto pool = VulkanDescriptorPool::Builder(_context)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
            .setMaxSets(1)
            .build();

        VkDescriptorSet descriptorSet = pool->allocateSet(*_prefilterLayout);
        VkImageView mipView = _context.createImageView(
            prefilteredMap.getImage(),
            prefilteredMap.getFormat(),
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            6,
            0,
            1,
            mip);

        VkDescriptorImageInfo storageImageInfo{};
        storageImageInfo.imageView = mipView;
        storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        DescriptorWriter(_context, descriptorSet)
            .writeImage(0, &envMapInfo)
            .writeImage(1, &storageImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            .update();

        _uploader.submit([&](VkCommandBuffer cmd) {
            _prefilterPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                _prefilterPipeline->getLayout(),
                0,
                1,
                &descriptorSet,
                0,
                nullptr);

            const float roughness =
                mipLevels > 1 ? static_cast<float>(mip) / static_cast<float>(mipLevels - 1) : 0.0f;
            vkCmdPushConstants(
                cmd,
                _prefilterPipeline->getLayout(),
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(float),
                &roughness);

            const uint32_t mipWidth = std::max(1u, prefilteredMap.getExtent().width >> mip);
            const uint32_t mipHeight = std::max(1u, prefilteredMap.getExtent().height >> mip);
            vkCmdDispatch(cmd, ceilDiv(mipWidth, 32), ceilDiv(mipHeight, 32), 6);

            VkMemoryBarrier memoryBarrier{};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                1,
                &memoryBarrier,
                0,
                nullptr,
                0,
                nullptr);
        });

        vkDestroyImageView(_context.getDevice(), mipView, nullptr);
    }

    _uploader.submit([&](VkCommandBuffer cmd) {
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    LOG_INFO("IBL: prefiltered specular cubemap generated");
}

VulkanImage* IBLProcessor::getIrradianceMap(const std::string& name) const {
    const auto it = _ibTextures.find(name);
    return it == _ibTextures.end() ? nullptr : it->second.irradianceMap.get();
}

VulkanImage* IBLProcessor::getPrefilteredMap(const std::string& name) const {
    const auto it = _ibTextures.find(name);
    return it == _ibTextures.end() ? nullptr : it->second.prefilteredMap.get();
}

VulkanImage* IBLProcessor::getBrdfLUT() const {
    return _brdfLUT.get();
}
