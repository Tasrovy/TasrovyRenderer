#pragma once
#include "VulkanContext.h"
#include <cstddef>
#include <vulkan/vulkan.h>
class VulkanBuffer{
private:
    VkBuffer _buffer;
    VkDeviceMemory _memory;
    VkDeviceSize _size;
    VkBufferUsageFlags _usage;
    VkMemoryPropertyFlags _properties;
    void* _mappedMemory;
    VulkanContext* _context;
public:
    VulkanBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    ~VulkanBuffer();
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    void setData(void* pointer,size_t size);
    void setData(const void* data, VkDeviceSize size, VkDeviceSize offset);
    void* getMappedMemory() { return _mappedMemory; }
    VkBuffer getBuffer() const { return _buffer; }
    VkDeviceMemory getMemory() { return _memory; }
    VkDeviceSize getSize() { return _size; }
    VkBufferUsageFlags getUsage() { return _usage; }
    VkMemoryPropertyFlags getProperties() { return _properties; }
    VkDescriptorBufferInfo getDescriptorInfo() const { return {_buffer,0,_size}; }
};