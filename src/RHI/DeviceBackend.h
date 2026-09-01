#pragma once

#include "CommandListBackend.h"
#include "Device.h"
#include "FrameSchedulerBackend.h"
#include "ResourceBackend.h"

#include <memory>

namespace Tasrovy::RHI {

class IDeviceBackend {
public:
    virtual ~IDeviceBackend() = default;

    virtual std::unique_ptr<IFrameSchedulerBackend> createFrameScheduler() = 0;
    virtual std::unique_ptr<ICommandListBackend> createCommandList() = 0;
    virtual std::unique_ptr<IBufferBackend> createBuffer(
        const BufferDesc& desc) = 0;
    virtual std::unique_ptr<IImageBackend> createTexture(
        const ImageUploadDesc& upload) = 0;
    virtual std::unique_ptr<IImageBackend> createSolidTexture(
        const std::array<float, 4>& color, Format format) = 0;
    virtual std::unique_ptr<IImageBackend> createAttachment(
        uint32_t width, uint32_t height, Format format,
        bool storage, bool useDeviceMsaa) = 0;
    virtual std::unique_ptr<IImageBackend> createImage2D(
        uint32_t width, uint32_t height, Format format) = 0;
    virtual std::unique_ptr<IImageBackend> createVirtualShadowMap(
        const VirtualShadowMapDesc& desc) = 0;
    virtual std::unique_ptr<IPipelineBackend> createGraphicsPipeline(
        const PipelineDesc& desc) = 0;
    virtual std::unique_ptr<IPipelineBackend> createComputePipeline(
        const ComputePipelineDesc& desc) = 0;
    virtual std::unique_ptr<IDescriptorSetLayoutBackend>
        createDescriptorSetLayout(const DescriptorSetDesc& desc) = 0;
    virtual std::unique_ptr<IDescriptorPoolBackend> createDescriptorPool(
        uint32_t maxSets,
        const std::vector<DescriptorPoolSizeDesc>& poolSizes) = 0;
    virtual void updateDescriptorSet(
        const IDescriptorSetBackend& descriptorSet,
        const std::vector<DescriptorWriteDesc>& writes) = 0;
    virtual DescriptorImageInfo getIBLDescriptorInfo(
        IBLMapType mapType, const std::string& name) const = 0;
    virtual void createIBLMaps(IImageBackend& skybox, const std::string& name) = 0;
    virtual Format depthFormat() const = 0;
    virtual size_t deferredDeletionCount() const = 0;
    virtual BackendInteropContext interopContext() const = 0;
};

std::unique_ptr<IDeviceBackend> createSelectedDeviceBackend(
    const SurfaceDeviceCreateInfo& createInfo);

} // namespace Tasrovy::RHI
