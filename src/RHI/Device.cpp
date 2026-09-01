#include "Device.h"

#include "Buffer.h"
#include "CommandList.h"
#include "CommandListBackend.h"
#include "Descriptor.h"
#include "DeviceBackend.h"
#include "FrameScheduler.h"
#include "Image.h"
#include "Pipeline.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Tasrovy::RHI {

struct Device::Impl {
    std::unique_ptr<IDeviceBackend> backend;
    std::unique_ptr<FrameScheduler> frameScheduler;
    std::mutex resourceScopeMutex;
    ResourceScope nextResourceScope = 1;
    std::unordered_map<ResourceScope, std::vector<std::shared_ptr<void>>>
        resourceScopes;
};

std::shared_ptr<Device> Device::createForSurface(
    const SurfaceDeviceCreateInfo& createInfo) {
    auto device = std::shared_ptr<Device>(new Device());
    device->impl_ = std::make_unique<Impl>();
    device->impl_->backend = createSelectedDeviceBackend(createInfo);
    if (!device->impl_->backend)
        throw std::runtime_error("No RHI device backend was selected");
    device->impl_->frameScheduler = std::unique_ptr<FrameScheduler>(
        new FrameScheduler(
            device->impl_->backend->createFrameScheduler()));
    return device;
}

Device::~Device() {
    std::unordered_map<ResourceScope, std::vector<std::shared_ptr<void>>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        retired.swap(impl_->resourceScopes);
    }
    retired.clear();
    if (impl_->frameScheduler) impl_->frameScheduler->waitForInFlightFrames();
}

Device::ResourceScope Device::createResourceScope() {
    std::lock_guard lock(impl_->resourceScopeMutex);
    const ResourceScope scope = impl_->nextResourceScope++;
    impl_->resourceScopes.try_emplace(scope);
    return scope;
}
void Device::retainResourceUntyped(
    ResourceScope scope, std::shared_ptr<void> resource) {
    if (!resource) return;
    std::lock_guard lock(impl_->resourceScopeMutex);
    impl_->resourceScopes[scope].push_back(std::move(resource));
}
void Device::resetResourceScope(ResourceScope scope) {
    std::vector<std::shared_ptr<void>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        const auto found = impl_->resourceScopes.find(scope);
        if (found == impl_->resourceScopes.end()) return;
        retired.swap(found->second);
    }
}
void Device::destroyResourceScope(ResourceScope scope) {
    std::vector<std::shared_ptr<void>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        const auto found = impl_->resourceScopes.find(scope);
        if (found == impl_->resourceScopes.end()) return;
        retired = std::move(found->second);
        impl_->resourceScopes.erase(found);
    }
}

std::shared_ptr<Buffer> Device::createBuffer(const BufferDesc& desc) {
    return Buffer::CreateFromBackend(impl_->backend->createBuffer(desc));
}
std::shared_ptr<Buffer> Device::createVertexBuffer(uint64_t size) {
    return createBuffer({size, BufferUsage::Vertex, false});
}
std::shared_ptr<Buffer> Device::createIndexBuffer(uint64_t size) {
    return createBuffer({size, BufferUsage::Index, false});
}
std::shared_ptr<Buffer> Device::createUniformBuffer(uint64_t size) {
    return createBuffer({size, BufferUsage::Uniform, true});
}
std::shared_ptr<Buffer> Device::createStagingBuffer(uint64_t size) {
    return createBuffer({size, BufferUsage::TransferSource, true});
}
std::shared_ptr<CommandList> Device::createCommandList() {
    return CommandList::CreateFromBackend(
        impl_->backend->createCommandList());
}

std::shared_ptr<Image> Device::createTexture(const ImageUploadDesc& upload) {
    return Image::CreateFromBackend(impl_->backend->createTexture(upload));
}
std::shared_ptr<Image> Device::createSolidTexture(
    const std::array<float, 4>& color, Format format) {
    return Image::CreateFromBackend(
        impl_->backend->createSolidTexture(color, format));
}
std::shared_ptr<Image> Device::createAttachment(
    uint32_t width, uint32_t height, Format format) {
    return Image::CreateFromBackend(impl_->backend->createAttachment(
        width, height, format, false, true));
}
std::shared_ptr<Image> Device::createImage2D(
    uint32_t width, uint32_t height, Format format) {
    return Image::CreateFromBackend(
        impl_->backend->createImage2D(width, height, format));
}
std::shared_ptr<Image> Device::createRenderTexture(
    const RenderTextureDesc& desc) {
    if (desc.external) return nullptr;
    return Image::CreateFromBackend(impl_->backend->createAttachment(
        desc.width, desc.height, resolveRenderTextureFormat(desc.format),
        desc.storage, false));
}
std::shared_ptr<Image> Device::createVirtualShadowMap(
    const VirtualShadowMapDesc& desc) {
    if (desc.atlasSize == 0 || desc.pageSize == 0 ||
        desc.atlasSize % desc.pageSize != 0) return nullptr;
    const uint32_t pagesPerAxis = desc.atlasSize / desc.pageSize;
    if (desc.residentPageCount == 0 ||
        desc.residentPageCount > pagesPerAxis * pagesPerAxis) return nullptr;
    return Image::CreateFromBackend(
        impl_->backend->createVirtualShadowMap(desc));
}
Format Device::resolveRenderTextureFormat(RenderTextureFormat format) const {
    switch (format) {
    case RenderTextureFormat::RGBA8Unorm: return Format::RGBA8Unorm;
    case RenderTextureFormat::RGBA16Float: return Format::RGBA16Float;
    case RenderTextureFormat::RG16Float: return Format::RG16Float;
    case RenderTextureFormat::Depth32Float: return Format::Depth32Float;
    case RenderTextureFormat::Swapchain:
        return impl_->frameScheduler->getColorFormat();
    }
    return Format::RGBA8Unorm;
}

std::shared_ptr<Pipeline> Device::createGraphicsPipeline(
    const PipelineDesc& desc) {
    return Pipeline::CreateFromBackend(
        impl_->backend->createGraphicsPipeline(desc));
}
std::shared_ptr<Pipeline> Device::createComputePipeline(
    const ComputePipelineDesc& desc) {
    return Pipeline::CreateFromBackend(
        impl_->backend->createComputePipeline(desc));
}
std::shared_ptr<Pass> Device::createPass(PassDesc desc) {
    return Pass::create(std::move(desc));
}

std::shared_ptr<DescriptorSetLayout> Device::createDescriptorSetLayout(
    const DescriptorSetDesc& desc) {
    return DescriptorSetLayout::CreateFromBackend(
        impl_->backend->createDescriptorSetLayout(desc));
}
std::shared_ptr<DescriptorPool> Device::createDescriptorPool(
    uint32_t maxSets,
    const std::vector<DescriptorPoolSizeDesc>& poolSizes) {
    return DescriptorPool::CreateFromBackend(
        impl_->backend->createDescriptorPool(maxSets, poolSizes));
}
DescriptorSet Device::allocateDescriptorSet(
    DescriptorPool& pool, const DescriptorSetLayout& layout) {
    return pool.allocateSet(layout);
}
void Device::updateDescriptorSet(
    const DescriptorSet& descriptorSet,
    const std::vector<DescriptorWriteDesc>& writes) {
    impl_->backend->updateDescriptorSet(descriptorSet.backend(), writes);
}
DescriptorImageInfo Device::getIBLDescriptorInfo(
    IBLMapType mapType, const std::string& name) const {
    return impl_->backend->getIBLDescriptorInfo(mapType, name);
}
void Device::createIBLMaps(Image& skybox, const std::string& name) {
    impl_->backend->createIBLMaps(skybox.backend(), name);
}
FrameScheduler& Device::getFrameScheduler() {
    return *impl_->frameScheduler;
}
const FrameScheduler& Device::getFrameScheduler() const {
    return *impl_->frameScheduler;
}
Format Device::getDepthFormat() const { return impl_->backend->depthFormat(); }
size_t Device::getDeferredDeletionCount() const {
    return impl_->backend->deferredDeletionCount();
}
BackendInteropContext Device::getBackendInteropContext() const {
    return impl_->backend->interopContext();
}

} // namespace Tasrovy::RHI
