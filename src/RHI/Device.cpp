#include "Device.h"
#include "RHIConfig.h"
#include "ReflectionBridge.h"
#include "ReflectionData.h"
#include "../render/Material.h"
#include "../render/Shader.h"

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/VulkanBuffer.h"
    #include "../RHI/Vulkan/VulkanImage.h"
    #include "../RHI/Vulkan/VulkanPipeline.h"
    #include "../RHI/Vulkan/VulkanDescriptorSetLayout.h"
    #include "../RHI/Vulkan/VulkanDescriptorPool.h"
    #include "../RHI/Vulkan/VulkanSwapChain.h"
    #include "../RHI/Vulkan/ImmediateSubmitter.h"
    #include "../RHI/Vulkan/Renderer.h"
    #include "../RHI/Vulkan/VulkanQueue.h"
#endif

namespace Tasrovy {

struct Device::Impl {
#ifdef TASROVY_API_VULKAN
    VulkanContext* context = nullptr;
    ImmediateSubmitter* submitter = nullptr;
#endif
    std::shared_ptr<ReflectionBridge> reflectionBridge;
};

std::shared_ptr<Device> Device::create() {
    auto dev = std::shared_ptr<Device>(new Device());
    dev->impl_ = std::make_unique<Impl>();
    dev->impl_->reflectionBridge = std::make_shared<ReflectionBridge>();
    dev->impl_->reflectionBridge->start();
    return dev;
}

std::shared_ptr<Buffer> Device::createBuffer(const BufferDesc& desc) {
    // TODO: implement API dispatch
    return nullptr;
}

std::shared_ptr<Buffer> Device::createVertexBuffer(uint64_t size) {
    return createBuffer({ size, 0x1, false });
}

std::shared_ptr<Buffer> Device::createIndexBuffer(uint64_t size) {
    return createBuffer({ size, 0x2, false });
}

std::shared_ptr<Buffer> Device::createUniformBuffer(uint64_t size) {
    return createBuffer({ size, 0x10, true });
}

std::shared_ptr<Buffer> Device::createStagingBuffer(uint64_t size) {
    return createBuffer({ size, 0x4, true });
}

std::shared_ptr<Image> Device::createTexture(const std::string& path, bool generateMips) {
    return nullptr;
}

std::shared_ptr<Image> Device::createCubemap(const std::string& directoryPath) {
    return nullptr;
}

std::shared_ptr<Image> Device::createAttachment(uint32_t width, uint32_t height, uint32_t format) {
    return nullptr;
}

std::shared_ptr<Image> Device::createImage2D(uint32_t width, uint32_t height, uint32_t format) {
    return nullptr;
}

std::shared_ptr<Pipeline> Device::createGraphicsPipeline(const PipelineDesc& desc) {
    return nullptr;
}

std::shared_ptr<DescriptorSetLayout> Device::createDescriptorSetLayout(
    const std::vector<uint32_t>& bindingTypes) {
    return nullptr;
}

std::shared_ptr<DescriptorPool> Device::createDescriptorPool(
    uint32_t maxSets, const std::vector<uint32_t>& poolSizes) {
    return nullptr;
}

void Device::uploadBuffer(Buffer& dst, const void* data, uint64_t size) {
}

// --- Shader Reflection ---

std::shared_ptr<ReflectionBridge> Device::getReflectionBridge() {
    return impl_->reflectionBridge;
}

void Device::requestShaderReflection(Material* material) {
    if (!material || !impl_->reflectionBridge) return;
    auto shader = material->getShader();
    if (!shader) return;

    ReflectionRequest req;
    req.material = material;
    req.vertSpvPath = shader->getVertexPath();
    req.fragSpvPath = shader->getFragmentPath();
    req.vertEntryPoint = shader->getVertexEntry();
    req.fragEntryPoint = shader->getFragmentEntry();

    impl_->reflectionBridge->submitRequest(std::move(req));
}

void Device::processReflectionResults() {
    if (impl_->reflectionBridge) {
        impl_->reflectionBridge->pollResults();
    }
}

} // namespace Tasrovy
