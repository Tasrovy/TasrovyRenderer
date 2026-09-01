#pragma once

#include "../ResourceBackend.h"

#include <memory>

class VulkanBuffer;
class VulkanDescriptorPool;
class VulkanDescriptorSetLayout;
class VulkanImage;
class VulkanPipeline;

namespace Tasrovy::RHI::Vulkan {

class VulkanBufferBackend final : public IBufferBackend {
public:
    explicit VulkanBufferBackend(std::unique_ptr<VulkanBuffer> buffer);
    ~VulkanBufferBackend() override;
    uint64_t size() const override;
    void setData(const void* data, uint64_t size, uint64_t offset) override;
    void* mappedMemory() override;
    DescriptorBufferInfo descriptorInfo() const override;
    uint64_t nativeHandle() const override;
private:
    std::unique_ptr<VulkanBuffer> buffer_;
};

class VulkanImageBackend final : public IImageBackend {
public:
    explicit VulkanImageBackend(std::unique_ptr<VulkanImage> image);
    ~VulkanImageBackend() override;
    Format format() const override;
    uint32_t mipLevels() const override;
    DescriptorImageInfo descriptorInfo() const override;
    DescriptorImageInfo storageDescriptorInfo() const override;
    uint64_t nativeImage() const override;
    uint64_t nativeView() const override;
    uint64_t nativeSampler() const override;
    VulkanImage& value() const;
private:
    std::unique_ptr<VulkanImage> image_;
};

class VulkanPipelineBackend final : public IPipelineBackend {
public:
    explicit VulkanPipelineBackend(std::unique_ptr<VulkanPipeline> pipeline);
    ~VulkanPipelineBackend() override;
    uint64_t nativePipeline() const override;
    uint64_t nativeLayout() const override;
private:
    std::unique_ptr<VulkanPipeline> pipeline_;
};

class VulkanDescriptorSetLayoutBackend final
    : public IDescriptorSetLayoutBackend {
public:
    explicit VulkanDescriptorSetLayoutBackend(
        std::unique_ptr<VulkanDescriptorSetLayout> layout);
    ~VulkanDescriptorSetLayoutBackend() override;
    uint64_t nativeLayout() const override;
    VulkanDescriptorSetLayout& value() const;
private:
    std::unique_ptr<VulkanDescriptorSetLayout> layout_;
};

class VulkanDescriptorSetBackend final : public IDescriptorSetBackend {
public:
    explicit VulkanDescriptorSetBackend(uint64_t descriptorSet);
    uint64_t nativeSet() const override;
private:
    uint64_t descriptorSet_ = 0;
};

class VulkanDescriptorPoolBackend final : public IDescriptorPoolBackend {
public:
    explicit VulkanDescriptorPoolBackend(
        std::unique_ptr<VulkanDescriptorPool> pool);
    ~VulkanDescriptorPoolBackend() override;
    std::shared_ptr<IDescriptorSetBackend> allocateSet(
        const IDescriptorSetLayoutBackend& layout) override;
private:
    std::unique_ptr<VulkanDescriptorPool> pool_;
};

} // namespace Tasrovy::RHI::Vulkan
