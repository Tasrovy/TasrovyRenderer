#pragma once

#include "../DeviceBackend.h"

#include <memory>

class IBLProcessor;
class ImmediateSubmitter;
class Renderer;
class VulkanContext;
class VulkanQueue;
class VulkanSwapchain;

namespace Tasrovy::RHI::Vulkan {

class VulkanDeviceBackend final : public IDeviceBackend {
public:
    explicit VulkanDeviceBackend(const SurfaceDeviceCreateInfo& createInfo);
    ~VulkanDeviceBackend() override;

    std::unique_ptr<IFrameSchedulerBackend> createFrameScheduler() override;
    std::unique_ptr<ICommandListBackend> createCommandList() override;
    std::unique_ptr<IBufferBackend> createBuffer(
        const BufferDesc& desc) override;
    std::unique_ptr<IImageBackend> createTexture(
        const ImageUploadDesc& upload) override;
    std::unique_ptr<IImageBackend> createSolidTexture(
        const std::array<float, 4>& color, Format format) override;
    std::unique_ptr<IImageBackend> createAttachment(
        uint32_t width, uint32_t height, Format format,
        bool storage, bool useDeviceMsaa) override;
    std::unique_ptr<IImageBackend> createImage2D(
        uint32_t width, uint32_t height, Format format) override;
    std::unique_ptr<IImageBackend> createVirtualShadowMap(
        const VirtualShadowMapDesc& desc) override;
    std::unique_ptr<IPipelineBackend> createGraphicsPipeline(
        const PipelineDesc& desc) override;
    std::unique_ptr<IPipelineBackend> createComputePipeline(
        const ComputePipelineDesc& desc) override;
    std::unique_ptr<IDescriptorSetLayoutBackend>
        createDescriptorSetLayout(const DescriptorSetDesc& desc) override;
    std::unique_ptr<IDescriptorPoolBackend> createDescriptorPool(
        uint32_t maxSets,
        const std::vector<DescriptorPoolSizeDesc>& poolSizes) override;
    void updateDescriptorSet(
        const IDescriptorSetBackend& descriptorSet,
        const std::vector<DescriptorWriteDesc>& writes) override;
    DescriptorImageInfo getIBLDescriptorInfo(
        IBLMapType mapType, const std::string& name) const override;
    void createIBLMaps(
        IImageBackend& skybox, const std::string& name) override;
    Format depthFormat() const override;
    size_t deferredDeletionCount() const override;
    BackendInteropContext interopContext() const override;

private:
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<VulkanQueue> graphicsQueue_;
    std::unique_ptr<VulkanQueue> presentQueue_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<ImmediateSubmitter> submitter_;
    std::unique_ptr<VulkanSwapchain> swapchain_;
    std::unique_ptr<IBLProcessor> ibl_;
    bool schedulerCreated_ = false;
};

} // namespace Tasrovy::RHI::Vulkan
