#include "DescriptorWriter.h"
#include <stdexcept>

DescriptorWriter::DescriptorWriter(VulkanContext& context, VkDescriptorSet targetSet)
    : _context(context), _targetSet(targetSet) {}

DescriptorWriter& DescriptorWriter::writeBuffer(uint32_t binding, const VkDescriptorBufferInfo* bufferInfo) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _targetSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // 注意：这里可以根据需要变得更通用
    if(bufferInfo->range == VK_WHOLE_SIZE) write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = bufferInfo;

    _writes.push_back(write);
    return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding, const VkDescriptorImageInfo* imageInfo) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _targetSet;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = imageInfo;

    _writes.push_back(write);
    return *this;
}

DescriptorWriter& DescriptorWriter::writeImage(uint32_t binding, const VkDescriptorImageInfo* imageInfo, VkDescriptorType descriptorType) {
    if (imageInfo == nullptr) {
        throw std::runtime_error("imageInfo cannot be null in writeImage!");
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _targetSet;
    write.dstBinding = binding;
    write.descriptorType = descriptorType;
    write.descriptorCount = 1;
    write.pImageInfo = imageInfo;

    _writes.push_back(write);
    return *this;
}

void DescriptorWriter::update() {
    vkUpdateDescriptorSets(_context.getDevice(), static_cast<uint32_t>(_writes.size()), _writes.data(), 0, nullptr);
    _writes.clear(); // 清空以便复用
}