#include "VulkanBuffer.h"
#include "../ResourceTracker.h"
#include <stdexcept>
VulkanBuffer::VulkanBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties){
    this->_size = size;
    this->_usage = usage;
    this->_properties = properties;
    this->_context = &context;
    this->_mappedMemory = nullptr;
    bool needmap = _properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    if(!needmap) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(_context->getDevice(), &bufferInfo, nullptr, &_buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(_context->getDevice(), _buffer, &memRequirements);
    _allocationSize = memRequirements.size;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _context->findMemoryType(memRequirements.memoryTypeBits, _properties);
    if (vkAllocateMemory(_context->getDevice(), &allocInfo, nullptr, &_memory) != VK_SUCCESS) {
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate buffer memory!");
    }
    if (vkBindBufferMemory(_context->getDevice(), _buffer, _memory, 0) != VK_SUCCESS) {
        vkFreeMemory(_context->getDevice(), _memory, nullptr);
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _memory = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to bind buffer memory!");
    }
    if (needmap && vkMapMemory(_context->getDevice(), _memory, 0, size, 0, &_mappedMemory) != VK_SUCCESS) {
        vkFreeMemory(_context->getDevice(), _memory, nullptr);
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _memory = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to map buffer memory!");
    }
    Tasrovy::RHI::ResourceTracker::created(
        Tasrovy::RHI::TrackedResourceKind::Buffer,
        static_cast<uint64_t>(_allocationSize));
}
VulkanBuffer::~VulkanBuffer(){
    if (!_context) {
        return;
    }

    if (_mappedMemory) {
        vkUnmapMemory(_context->getDevice(), _memory);
        _mappedMemory = nullptr;
    }

    VkBuffer buffer = _buffer;
    VkDeviceMemory memory = _memory;
    const VkDeviceSize allocationSize = _allocationSize;
    _buffer = VK_NULL_HANDLE;
    _memory = VK_NULL_HANDLE;
    _allocationSize = 0;

    _context->deferDelete([buffer, memory, allocationSize](VkDevice device) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
        if (buffer != VK_NULL_HANDLE || memory != VK_NULL_HANDLE) {
            Tasrovy::RHI::ResourceTracker::destroyed(
                Tasrovy::RHI::TrackedResourceKind::Buffer,
                static_cast<uint64_t>(allocationSize));
        }
    });
}
void VulkanBuffer::setData(void* pointer,size_t size){
    memcpy(_mappedMemory, pointer, size);
}
void VulkanBuffer::setData(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    // 安全检查
    if (_mappedMemory == nullptr) {
        throw std::runtime_error("Cannot write to a buffer that is not mapped!");
    }
    if (size + offset > _size) {
        throw std::runtime_error("Write operation exceeds buffer capacity!");
    }

    // vvvvvvvvvvvvvvvv 关键修改 vvvvvvvvvvvvvvvv
    // 计算出目标地址：起始地址 + 偏移量
    char* memOffset = static_cast<char*>(_mappedMemory) + offset;
    memcpy(memOffset, data, static_cast<size_t>(size));
    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}
