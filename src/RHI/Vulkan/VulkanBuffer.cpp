#include "VulkanBuffer.h"
#include "../ResourceTracker.h"
#include <Logger.hpp>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
VulkanBuffer::VulkanBuffer(
    VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    std::string debugName)
    : _debugName(std::move(debugName)) {
    this->_size = size;
    this->_properties = properties;
    this->_context = &context;
    this->_mappedMemory = nullptr;
    bool needmap = _properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    if(!needmap) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    this->_usage = usage;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const VkResult creationResult = vkCreateBuffer(
        _context->getDevice(), &bufferInfo, nullptr, &_buffer);
    if (creationResult != VK_SUCCESS) {
        LOG_GPU_MEMORY(
            "[ALLOC_FAILED] type=Buffer stage=vkCreateBuffer result={} "
            "name='{}' requestedBytes={} usage=0x{:x} properties=0x{:x}",
            static_cast<int32_t>(creationResult),
            _debugName.empty() ? "UnnamedBuffer" : _debugName,
            static_cast<uint64_t>(size),
            static_cast<uint32_t>(bufferInfo.usage),
            static_cast<uint32_t>(_properties));
        throw std::runtime_error("failed to create buffer!");
    }
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(_context->getDevice(), _buffer, &memRequirements);
    _allocationSize = memRequirements.size;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = _context->findMemoryType(memRequirements.memoryTypeBits, _properties);
    _memoryTypeIndex = allocInfo.memoryTypeIndex;
    const VkResult allocationResult = vkAllocateMemory(
        _context->getDevice(), &allocInfo, nullptr, &_memory);
    if (allocationResult != VK_SUCCESS) {
        LOG_GPU_MEMORY(
            "[ALLOC_FAILED] type=Buffer stage=vkAllocateMemory result={} "
            "name='{}' requestedBytes={} "
            "allocationBytes={} usage=0x{:x} properties=0x{:x} memoryType={}",
            static_cast<int32_t>(allocationResult),
            _debugName.empty() ? "UnnamedBuffer" : _debugName,
            static_cast<uint64_t>(size),
            static_cast<uint64_t>(memRequirements.size),
            static_cast<uint32_t>(bufferInfo.usage),
            static_cast<uint32_t>(_properties),
            allocInfo.memoryTypeIndex);
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate buffer memory!");
    }
    const VkResult bindResult = vkBindBufferMemory(
        _context->getDevice(), _buffer, _memory, 0);
    if (bindResult != VK_SUCCESS) {
        LOG_GPU_MEMORY(
            "[ALLOC_FAILED] type=Buffer stage=vkBindBufferMemory result={} "
            "name='{}' buffer=0x{:x} memory=0x{:x} allocationBytes={}",
            static_cast<int32_t>(bindResult),
            _debugName.empty() ? "UnnamedBuffer" : _debugName,
            reinterpret_cast<uintptr_t>(_buffer),
            reinterpret_cast<uintptr_t>(_memory),
            static_cast<uint64_t>(memRequirements.size));
        vkFreeMemory(_context->getDevice(), _memory, nullptr);
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _memory = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to bind buffer memory!");
    }
    const VkResult mapResult = needmap
        ? vkMapMemory(
            _context->getDevice(), _memory, 0, size, 0, &_mappedMemory)
        : VK_SUCCESS;
    if (mapResult != VK_SUCCESS) {
        LOG_GPU_MEMORY(
            "[ALLOC_FAILED] type=Buffer stage=vkMapMemory result={} "
            "name='{}' memory=0x{:x} requestedBytes={} allocationBytes={}",
            static_cast<int32_t>(mapResult),
            _debugName.empty() ? "UnnamedBuffer" : _debugName,
            reinterpret_cast<uintptr_t>(_memory),
            static_cast<uint64_t>(size),
            static_cast<uint64_t>(memRequirements.size));
        vkFreeMemory(_context->getDevice(), _memory, nullptr);
        vkDestroyBuffer(_context->getDevice(), _buffer, nullptr);
        _memory = VK_NULL_HANDLE;
        _buffer = VK_NULL_HANDLE;
        throw std::runtime_error("failed to map buffer memory!");
    }
    Tasrovy::RHI::ResourceTracker::created(
        Tasrovy::RHI::TrackedResourceKind::Buffer,
        static_cast<uint64_t>(_allocationSize));
    const auto tracker = Tasrovy::RHI::ResourceTracker::snapshot();
    const auto& bufferStats = tracker.resources[static_cast<size_t>(
        Tasrovy::RHI::TrackedResourceKind::Buffer)];
    LOG_GPU_MEMORY(
        "[ALLOC] type=Buffer buffer=0x{:x} memory=0x{:x} name='{}' requestedBytes={} "
        "allocationBytes={} usage=0x{:x} properties=0x{:x} memoryType={} "
        "bufferLiveBytes={} bufferPeakBytes={} totalLiveBytes={}",
        reinterpret_cast<uintptr_t>(_buffer),
        reinterpret_cast<uintptr_t>(_memory),
        _debugName.empty() ? "UnnamedBuffer" : _debugName,
        static_cast<uint64_t>(_size),
        static_cast<uint64_t>(_allocationSize),
        static_cast<uint32_t>(bufferInfo.usage),
        static_cast<uint32_t>(_properties),
        _memoryTypeIndex,
        bufferStats.liveBytes,
        bufferStats.peakBytes,
        tracker.totalLiveBytes);
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
    const VkDeviceSize requestedSize = _size;
    const VkBufferUsageFlags usage = _usage;
    const VkMemoryPropertyFlags properties = _properties;
    const uint32_t memoryTypeIndex = _memoryTypeIndex;
    const std::string debugName = _debugName;
    LOG_GPU_MEMORY(
        "[RETIRE] type=Buffer buffer=0x{:x} memory=0x{:x} name='{}' requestedBytes={} "
        "allocationBytes={} usage=0x{:x} properties=0x{:x} memoryType={}",
        reinterpret_cast<uintptr_t>(buffer),
        reinterpret_cast<uintptr_t>(memory),
        debugName.empty() ? "UnnamedBuffer" : debugName,
        static_cast<uint64_t>(requestedSize),
        static_cast<uint64_t>(allocationSize),
        static_cast<uint32_t>(usage),
        static_cast<uint32_t>(properties),
        memoryTypeIndex);
    _buffer = VK_NULL_HANDLE;
    _memory = VK_NULL_HANDLE;
    _allocationSize = 0;

    _context->deferDelete([buffer, memory, allocationSize, requestedSize,
                           usage, properties, memoryTypeIndex,
                           debugName](VkDevice device) {
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
            const auto tracker = Tasrovy::RHI::ResourceTracker::snapshot();
            const auto& bufferStats = tracker.resources[static_cast<size_t>(
                Tasrovy::RHI::TrackedResourceKind::Buffer)];
            LOG_GPU_MEMORY(
                "[FREE] type=Buffer buffer=0x{:x} memory=0x{:x} name='{}' "
                "requestedBytes={} allocationBytes={} usage=0x{:x} "
                "properties=0x{:x} memoryType={} bufferLiveBytes={} "
                "bufferPeakBytes={} totalLiveBytes={}",
                reinterpret_cast<uintptr_t>(buffer),
                reinterpret_cast<uintptr_t>(memory),
                debugName.empty() ? "UnnamedBuffer" : debugName,
                static_cast<uint64_t>(requestedSize),
                static_cast<uint64_t>(allocationSize),
                static_cast<uint32_t>(usage),
                static_cast<uint32_t>(properties),
                memoryTypeIndex,
                bufferStats.liveBytes,
                bufferStats.peakBytes,
                tracker.totalLiveBytes);
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
