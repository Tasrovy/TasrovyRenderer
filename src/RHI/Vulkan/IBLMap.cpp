#include "IBLMap.h"
#include "ImmediateSubmitter.h"
#include "VulkanBuffer.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanDescriptorPool.h"
#include "DescriptorWriter.h"
#include "VulkanPipeline.h"
#include <Logger.hpp>

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

    // Ϊ Push Constant ���巶Χ
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float); // ���ǽ�����һ�� float ���͵� roughness

    // �������� Push Constant �� PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[] = { _prefilterLayout->getLayout() };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    // ֱ�Ӵ��� Pipeline �� Layout����Ϊ PipelineBuilder ���ܲ�֧�� Push Constants
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
    VulkanImage& environmentCubemap, // ����: ԭʼ HDR ������ͼ
    VulkanImage& irradianceMap       // ���: Ŀ����ն�ͼ
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
    LOG_INFO("Generate IrradianceMap");
}

void IBLProcessor::generateBrdfMap() {
    LOG_INFO("Generating BRDF LUT...");

    // 1. ���� BRDF LUT ͼ����Դ
    _brdfLUT = VulkanImage::createImage2D(_context, { 512, 512 }, VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // 2. ����һ����ʱ���������غͼ�
    auto pool = VulkanDescriptorPool::Builder(_context)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();

    VkDescriptorSet descriptorSet = pool->allocateSet(*_brdfLutLayout);

    // 3. ���������������� BRDF LUT ��Ϊ storage image
    VkDescriptorImageInfo lutImageInfo = _brdfLUT->getDescriptorInfoForStorage();
    DescriptorWriter(_context, descriptorSet)
        .writeImage(0, &lutImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .update();

    // 4. ʹ�� uploader �ύ��������
    _uploader.submit([&](VkCommandBuffer cmd) {
        // a. �����ͼ��Ĳ���ת��Ϊ GENERAL����׼��д��
        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

        // b. �󶨹��ߺ���������
        _brdfLutPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _brdfLutPipeline->getLayout(), 0, 1, &descriptorSet, 0, nullptr);

        // c. Dispatch ��������
        //    �߳�������� = �����ߴ� / �ֲ��߳����С (����ȡ��)
        uint32_t dim = _brdfLUT->getExtent().width;
        vkCmdDispatch(cmd, dim / 32, dim / 32, 1); // 2D ������Z ά���� 1

        // d. �����ͼ��Ĳ���ת��Ϊ SHADER_READ_ONLY���Ա� PBR Ƭ����ɫ������
        _brdfLUT->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    LOG_INFO("BRDF LUT generated.");
}

void IBLProcessor::generatePrefilteredMap(VulkanImage& environmentCubemap, VulkanImage& prefilteredMap) {
    LOG_INFO("Generating Pre-filtered Specular Map...");

    uint32_t mipLevels = prefilteredMap.getMipLevels();
    if (mipLevels <= 1) {
        LOG_WARN("Prefiltered map has only 1 mip level. Skipping generation.");
        return;
    }

    // --- ׼��һ���Ե����������� ---
    // �󶨲���ı�����뻷��ͼ (binding 0)
    VkDescriptorImageInfo envMapInfo = environmentCubemap.getDescriptorInfo();
    DescriptorWriter(_context, _prefilterSet)
        .writeImage(0, &envMapInfo) // combined image sampler
        .update();

    // --- �ύһ���������� Mip Level ������������ ---
    _uploader.submit([&](VkCommandBuffer cmd) {
        // 1. ������ prefilteredMap ת��Ϊ GENERAL ���֣���׼���������� Mip ���д��
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

        // 2. �󶨹��ߣ���Ϊ����ѭ������ͬһ��
        _prefilterPipeline->bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

        // --- ѭ���������� Mipmap �ȼ� ---
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            // a. Ϊ��ǰ Mip Level ����һ��ר�ŵ� ImageView
            //    �����ͼֻ�������� prefilteredMap �ĵ� mip ��
            VkImageView mipView = _context.createImageView(
                prefilteredMap.getImage(),
                prefilteredMap.getFormat(),
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_VIEW_TYPE_CUBE,
                6,           // layerCount: ����Ҫд������6����
                0,           // baseArrayLayer
                1,           // levelCount: �����ͼֻ���� 1 �� mip level
                mip          // baseMipLevel: �ӵ� mip �� level ��ʼ
            );

            // b. ������������������� (binding 1) ָ������µ� Mip ��ͼ
            VkDescriptorImageInfo storageImageInfo = {};
            storageImageInfo.imageView = mipView;
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            DescriptorWriter(_context, _prefilterSet)
                .writeImage(1, &storageImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .update();

            // c. �󶨸��º����������
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _prefilterPipeline->getLayout(), 0, 1, &_prefilterSet, 0, nullptr);

            // d. ���㵱ǰ Mip ��Ӧ�� roughness����ͨ�� Push Constant ���ݸ���ɫ��
            float roughness = (float)mip / (float)(mipLevels - 1);
            vkCmdPushConstants(cmd, _prefilterPipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

            // e. Dispatch �������񣬴�С���ݵ�ǰ Mip �ĳߴ����
            uint32_t mipWidth = prefilteredMap.getExtent().width >> mip;
            uint32_t mipHeight = prefilteredMap.getExtent().height >> mip;
            // ȷ������ dispatch һ���߳���
            vkCmdDispatch(cmd, std::max(1u, mipWidth / 32), std::max(1u, mipHeight / 32), 6);

            // f. ������ʱ�� Mip ��ͼ
            vkDestroyImageView(_context.getDevice(), mipView, nullptr);

            // g. (��ѡ���Ƽ�) ��ÿ�� dispatch ֮�����һ������
            //    ȷ��ǰһ�� mip level ��д��Ժ��������������Ҫ�Ļ����ɼ�
            //    �������������ѭ����Ҳ����ʡ�ԣ���ѭ�����һ���ܵ�����
            VkMemoryBarrier memoryBarrier{};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
        }

        // 3. ���� Mip ������Ϻ󣬽����� prefilteredMap ת��Ϊ�ʺ���ɫ�������Ĳ���
        prefilteredMap.recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    LOG_INFO("Pre-filtered Specular Map generated.");
}

VulkanImage* IBLProcessor::getIrradianceMap(const std::string& name) const {
    // 1. ʹ�� map::find() ����ȫ�ز��� key
    //    map::operator[] �� key ������ʱ�ᴴ��һ����Ԫ�أ����� const �������ǲ�������
    auto it = _ibTextures.find(name);

    // 2. ����Ƿ��ҵ��˶�Ӧ����պ�
    if (it != _ibTextures.end()) {
        // it->second ָ�� map �е� IBTextures ����
        // .get() �� unique_ptr �л�ȡԭʼָ��
        return it->second.irradianceMap.get();
    }

    // 3. ���û���ҵ������ؿ�ָ��
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
    // BRDF LUT ��һ�������ĳ�Ա��ֱ�ӷ�������ԭʼָ��
    return _brdfLUT.get();
}