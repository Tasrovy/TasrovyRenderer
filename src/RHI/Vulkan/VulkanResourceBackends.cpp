#include "VulkanResourceBackends.h"

#include "VulkanBuffer.h"
#include "VulkanConversions.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanImage.h"
#include "VulkanPipeline.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI::Vulkan {

VulkanBufferBackend::VulkanBufferBackend(
    std::unique_ptr<VulkanBuffer> buffer)
    : buffer_(std::move(buffer)) {}
VulkanBufferBackend::~VulkanBufferBackend() = default;
uint64_t VulkanBufferBackend::size() const { return buffer_->getSize(); }
void VulkanBufferBackend::setData(
    const void* data, uint64_t size, uint64_t offset) {
    buffer_->setData(data, static_cast<VkDeviceSize>(size),
        static_cast<VkDeviceSize>(offset));
}
void* VulkanBufferBackend::mappedMemory() { return buffer_->getMappedMemory(); }
DescriptorBufferInfo VulkanBufferBackend::descriptorInfo() const {
    const auto info = buffer_->getDescriptorInfo();
    return {reinterpret_cast<uint64_t>(info.buffer), info.offset, info.range};
}
uint64_t VulkanBufferBackend::nativeHandle() const {
    return reinterpret_cast<uint64_t>(buffer_->getBuffer());
}

VulkanImageBackend::VulkanImageBackend(std::unique_ptr<VulkanImage> image)
    : image_(std::move(image)) {}
VulkanImageBackend::~VulkanImageBackend() = default;
Format VulkanImageBackend::format() const {
    return fromVkFormat(image_->getFormat());
}
uint32_t VulkanImageBackend::mipLevels() const {
    return image_->getMipLevels();
}
DescriptorImageInfo VulkanImageBackend::descriptorInfo() const {
    const auto info = image_->getDescriptorInfo();
    return {
        reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        fromVkImageLayout(info.imageLayout)};
}
DescriptorImageInfo VulkanImageBackend::storageDescriptorInfo() const {
    const auto info = image_->getDescriptorInfoForStorage();
    return {
        reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        fromVkImageLayout(info.imageLayout)};
}
uint64_t VulkanImageBackend::nativeImage() const {
    return reinterpret_cast<uint64_t>(image_->getImage());
}
uint64_t VulkanImageBackend::nativeView() const {
    return reinterpret_cast<uint64_t>(image_->getView());
}
uint64_t VulkanImageBackend::nativeSampler() const {
    return reinterpret_cast<uint64_t>(image_->getSampler());
}
VulkanImage& VulkanImageBackend::value() const { return *image_; }

VulkanPipelineBackend::VulkanPipelineBackend(
    std::unique_ptr<VulkanPipeline> pipeline)
    : pipeline_(std::move(pipeline)) {}
VulkanPipelineBackend::~VulkanPipelineBackend() = default;
uint64_t VulkanPipelineBackend::nativePipeline() const {
    return reinterpret_cast<uint64_t>(pipeline_->getPipeline());
}
uint64_t VulkanPipelineBackend::nativeLayout() const {
    return reinterpret_cast<uint64_t>(pipeline_->getLayout());
}

VulkanDescriptorSetLayoutBackend::VulkanDescriptorSetLayoutBackend(
    std::unique_ptr<VulkanDescriptorSetLayout> layout)
    : layout_(std::move(layout)) {}
VulkanDescriptorSetLayoutBackend::~VulkanDescriptorSetLayoutBackend() = default;
uint64_t VulkanDescriptorSetLayoutBackend::nativeLayout() const {
    return reinterpret_cast<uint64_t>(layout_->getLayout());
}
VulkanDescriptorSetLayout& VulkanDescriptorSetLayoutBackend::value() const {
    return *layout_;
}

VulkanDescriptorSetBackend::VulkanDescriptorSetBackend(uint64_t descriptorSet)
    : descriptorSet_(descriptorSet) {}
uint64_t VulkanDescriptorSetBackend::nativeSet() const { return descriptorSet_; }

VulkanDescriptorPoolBackend::VulkanDescriptorPoolBackend(
    std::unique_ptr<VulkanDescriptorPool> pool)
    : pool_(std::move(pool)) {}
VulkanDescriptorPoolBackend::~VulkanDescriptorPoolBackend() = default;
std::shared_ptr<IDescriptorSetBackend>
VulkanDescriptorPoolBackend::allocateSet(
    const IDescriptorSetLayoutBackend& layout) {
    const auto* vkLayout = dynamic_cast<
        const VulkanDescriptorSetLayoutBackend*>(&layout);
    if (!vkLayout) {
        throw std::invalid_argument("Descriptor layout backend mismatch");
    }
    return std::make_shared<VulkanDescriptorSetBackend>(
        reinterpret_cast<uint64_t>(pool_->allocateSet(vkLayout->value())));
}

} // namespace Tasrovy::RHI::Vulkan
