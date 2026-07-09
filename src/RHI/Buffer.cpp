#include "Buffer.h"
#include "RHIConfig.h"

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/VulkanBuffer.h"
#endif

namespace Tasrovy::RHI {

struct Buffer::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanBuffer> vkBuffer;
#endif
};

Buffer::~Buffer() = default;

std::shared_ptr<Buffer> Buffer::CreateFromNative(void* nativeContext, uint64_t size, uint32_t usageFlags, bool hostVisible) {
#ifdef TASROVY_API_VULKAN
    auto* ctx = static_cast<VulkanContext*>(nativeContext);

    VkBufferUsageFlags vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (usageFlags & 0x1)  vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usageFlags & 0x2)  vkUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usageFlags & 0x4)  vkUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (usageFlags & 0x10) vkUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (usageFlags & 0x20) vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VkMemoryPropertyFlags vkProps = hostVisible
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto buf = std::shared_ptr<Buffer>(new Buffer());
    buf->impl_ = std::make_unique<Impl>();
    buf->impl_->vkBuffer = std::make_unique<VulkanBuffer>(*ctx, size, vkUsage, vkProps);
    return buf;
#else
    return nullptr;
#endif
}

std::shared_ptr<Buffer> Buffer::CreateStagingFromNative(void* nativeContext, uint64_t size) {
#ifdef TASROVY_API_VULKAN
    auto* ctx = static_cast<VulkanContext*>(nativeContext);
    auto buf = std::shared_ptr<Buffer>(new Buffer());
    buf->impl_ = std::make_unique<Impl>();
    buf->impl_->vkBuffer = std::make_unique<VulkanBuffer>(*ctx, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    return buf;
#else
    return nullptr;
#endif
}

uint64_t Buffer::getSize() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint64_t>(impl_->vkBuffer->getSize());
#else
    return 0;
#endif
}

void Buffer::setData(const void* data, uint64_t size) {
#ifdef TASROVY_API_VULKAN
    impl_->vkBuffer->setData(data, static_cast<VkDeviceSize>(size), 0);
#endif
}

void Buffer::setData(const void* data, uint64_t size, uint64_t offset) {
#ifdef TASROVY_API_VULKAN
    impl_->vkBuffer->setData(data, static_cast<VkDeviceSize>(size), static_cast<VkDeviceSize>(offset));
#endif
}

void* Buffer::getMappedMemory() {
#ifdef TASROVY_API_VULKAN
    return impl_->vkBuffer->getMappedMemory();
#else
    return nullptr;
#endif
}

uint64_t Buffer::getNativeHandle() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkBuffer->getBuffer());
#else
    return 0;
#endif
}

DescriptorBufferInfo Buffer::getDescriptorInfo() const {
#ifdef TASROVY_API_VULKAN
    auto info = impl_->vkBuffer->getDescriptorInfo();
    return {
        reinterpret_cast<uint64_t>(info.buffer),
        static_cast<uint64_t>(info.offset),
        static_cast<uint64_t>(info.range)
    };
#else
    return {};
#endif
}

} // namespace Tasrovy::RHI
