#pragma once
#include "VulkanContext.h"
#include <vector>

class DescriptorWriter {
public:
    DescriptorWriter(VulkanContext& context, VkDescriptorSet targetSet);

    // 绑定一个 Buffer
    DescriptorWriter& writeBuffer(uint32_t binding, const VkDescriptorBufferInfo* bufferInfo);
    
    // 绑定一个 Image (包含 Sampler)
    DescriptorWriter& writeImage(uint32_t binding, const VkDescriptorImageInfo* imageInfo);

    DescriptorWriter& writeImage(uint32_t binding, const VkDescriptorImageInfo* imageInfo, VkDescriptorType descriptorType);

    // 执行更新
    void update();

private:
    VulkanContext& _context;
    VkDescriptorSet _targetSet;
    std::vector<VkWriteDescriptorSet> _writes;
};