#include "Device.h"
#include "Buffer.h"
#include "Image.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "CommandList.h"
#include "RHIConfig.h"
#include "ReflectionBridge.h"
#include "ReflectionData.h"
#include "../render/Material.h"
#include "../render/Shader.h"
#include "../ui/UI.h"
#include "../window/Window.h"
#include <algorithm>
#include <utility>

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/ImmediateSubmitter.h"
    #include "../RHI/Vulkan/VulkanImage.h"
    #include "../RHI/Vulkan/VulkanPipeline.h"
    #include "../RHI/Vulkan/VulkanDescriptorSetLayout.h"
    #include "../RHI/Vulkan/VulkanDescriptorPool.h"
    #include "../RHI/Vulkan/VulkanQueue.h"
    #include "../RHI/Vulkan/VulkanSwapChain.h"
    #include "../RHI/Vulkan/Renderer.h"
    #include "../RHI/Vulkan/IBLMap.h"
    #include "../RHI/Vulkan/DescriptorWriter.h"
#endif

namespace Tasrovy::RHI {

struct Device::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanContext> ownedContext;
    std::unique_ptr<VulkanQueue> graphicsQueue;
    std::unique_ptr<VulkanQueue> presentQueue;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ImmediateSubmitter> ownedSubmitter;
    std::unique_ptr<VulkanSwapchain> swapchain;
    VulkanContext* context = nullptr;
    ImmediateSubmitter* submitter = nullptr;
    std::unique_ptr<IBLProcessor> ibl;
#endif
    std::shared_ptr<ReflectionBridge> reflectionBridge;
};

std::shared_ptr<Device> Device::create(void* nativeContext, void* nativeSubmitter) {
    auto dev = std::shared_ptr<Device>(new Device());
    dev->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    dev->impl_->context = static_cast<VulkanContext*>(nativeContext);
    dev->impl_->submitter = static_cast<ImmediateSubmitter*>(nativeSubmitter);
#endif
    dev->impl_->reflectionBridge = std::make_shared<ReflectionBridge>();
    dev->impl_->reflectionBridge->start();
    return dev;
}

std::shared_ptr<Device> Device::createForWindow(Tasrovy::Windowing::Window& window, uint32_t maxFramesInFlight) {
    auto dev = std::shared_ptr<Device>(new Device());
    dev->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    dev->impl_->ownedContext = std::make_unique<VulkanContext>(
        "Vulkan",
        window.getRequiredVulkanExtensions(),
        [&window](VkInstance inst) { return window.createVulkanSurface(inst); },
        window.getWidth(),
        window.getHeight());
    dev->impl_->context = dev->impl_->ownedContext.get();
    dev->impl_->context->updateFramebufferSize(window.getWidth(), window.getHeight());
    dev->impl_->graphicsQueue = std::make_unique<VulkanQueue>(*dev->impl_->context, QueueType::Graphics);
    dev->impl_->presentQueue = std::make_unique<VulkanQueue>(*dev->impl_->context, QueueType::Present);
    dev->impl_->renderer = std::make_unique<Renderer>(*dev->impl_->context, maxFramesInFlight);
    dev->impl_->ownedSubmitter = std::make_unique<ImmediateSubmitter>(*dev->impl_->context, *dev->impl_->graphicsQueue);
    dev->impl_->submitter = dev->impl_->ownedSubmitter.get();
    dev->impl_->swapchain = std::make_unique<VulkanSwapchain>(*dev->impl_->context);
#endif
    dev->impl_->reflectionBridge = std::make_shared<ReflectionBridge>();
    dev->impl_->reflectionBridge->start();
    return dev;
}

Device::~Device() {
    waitIdle();
}

// --- Buffer creation ---

std::shared_ptr<Buffer> Device::createBuffer(const BufferDesc& desc) {
    return Buffer::CreateFromNative(impl_->context, desc.size, desc.usageFlags, desc.hostVisible);
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

// --- Image creation ---

std::shared_ptr<Image> Device::createTexture(const std::string& path, bool generateMips, uint32_t format) {
    return Image::CreateTextureFromNative(impl_->context, impl_->submitter, path, generateMips, format);
}

std::shared_ptr<Image> Device::createCubemap(const std::string& directoryPath) {
    return Image::CreateCubemapFromNative(impl_->context, impl_->submitter, directoryPath);
}

std::shared_ptr<Image> Device::createAttachment(uint32_t width, uint32_t height, uint32_t format) {
#ifdef TASROVY_API_VULKAN
    return Image::CreateAttachmentFromNative(impl_->context, width, height, format,
        static_cast<uint32_t>(impl_->context->getMsaaSamples()));
#else
    return nullptr;
#endif
}

std::shared_ptr<Image> Device::createImage2D(uint32_t width, uint32_t height, uint32_t format) {
    return Image::CreateImage2DFromNative(impl_->context, width, height, format);
}

std::shared_ptr<Image> Device::createRenderTexture(const RenderTextureDesc& desc) {
    if (desc.external) {
        return nullptr;
    }
    return Image::CreateAttachmentFromNative(
        impl_->context,
        desc.width,
        desc.height,
        resolveRenderTextureFormat(desc.format),
        1);
}

uint32_t Device::resolveRenderTextureFormat(RenderTextureFormat format) const {
    switch (format) {
    case RenderTextureFormat::RGBA8Unorm:
        return FormatRGBA8Unorm;
    case RenderTextureFormat::RGBA16Float:
        return FormatRGBA16Float;
    case RenderTextureFormat::RG16Float:
        return FormatRG16Float;
    case RenderTextureFormat::Depth32Float:
        return FormatDepth32Float;
    case RenderTextureFormat::Swapchain:
        return getSwapchainColorFormat();
    }
    return FormatRGBA8Unorm;
}

// --- Pipeline ---

std::shared_ptr<Pipeline> Device::createGraphicsPipeline(const PipelineDesc& desc) {
#ifdef TASROVY_API_VULKAN
    PipelineBuilder builder(*impl_->context);
    builder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, desc.vertShaderPath, desc.vertEntryPoint.c_str());
    builder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, desc.fragShaderPath, desc.fragEntryPoint.c_str());

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = desc.vertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs;
    for (size_t i = 0; i < desc.attributeLocations.size(); ++i) {
        attrs.push_back({
            desc.attributeLocations[i],
            0,
            static_cast<VkFormat>(desc.attributeFormats[i]),
            desc.attributeOffsets[i]
        });
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = desc.vertexStride == 0 ? 0 : 1;
    vertexInput.pVertexBindingDescriptions = desc.vertexStride == 0 ? nullptr : &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();
    builder.setVertexInputState(vertexInput);

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = static_cast<VkPrimitiveTopology>(desc.topology);
    builder.setInputAssemblyState(assembly);

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = static_cast<VkCompareOp>(desc.depthCompareOp);
    builder.setDepthStencilState(depth);

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.rasterizationSamples = desc.useMSAA
        ? impl_->context->getMsaaSamples()
        : VK_SAMPLE_COUNT_1_BIT;
    builder.setMultisampleState(multisample);

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = static_cast<VkCullModeFlags>(desc.cullMode);
    raster.frontFace = static_cast<VkFrontFace>(desc.frontFace);
    builder.setRasterizationState(raster);

    if (desc.descriptorSetLayout) {
        builder.addDescriptorSetLayout(reinterpret_cast<VkDescriptorSetLayout>(desc.descriptorSetLayout->getNativeLayout()));
    }
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorAttachmentFormats.size());
    for (const auto format : desc.colorAttachmentFormats) {
        colorFormats.push_back(static_cast<VkFormat>(format));
    }
    auto depthFormat = desc.depthAttachmentFormat;
    if (depthFormat == 0 && (desc.depthTest || desc.depthWrite)) {
        depthFormat = static_cast<uint32_t>(impl_->context->findDepthFormat());
    }
    builder.setRenderingFormats(colorFormats, static_cast<VkFormat>(depthFormat));

    auto pipeline = builder.buildGraphicsPipeline();
    return Pipeline::CreateFromNative(pipeline.release());
#else
    return nullptr;
#endif
}

std::shared_ptr<Pass> Device::createPass(PassDesc desc) {
    return Pass::create(std::move(desc));
}

// --- Descriptors ---

std::shared_ptr<DescriptorSetLayout> Device::createDescriptorSetLayout(
    const DescriptorSetDesc& desc) {
#ifdef TASROVY_API_VULKAN
    VulkanDescriptorSetLayout::Builder builder(*impl_->context);
    for (uint32_t i = 0; i < desc.bindingTypes.size(); ++i) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if (desc.bindingTypes[i] == DescriptorResourceType::CombinedImageSampler) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (desc.bindingTypes[i] == DescriptorResourceType::StorageImage) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        auto stageFlags = i < desc.stageFlags.size()
            ? static_cast<VkShaderStageFlags>(desc.stageFlags[i])
            : VK_SHADER_STAGE_ALL;
        builder.addBinding(i, type, stageFlags);
    }
    auto layout = builder.build();
    return DescriptorSetLayout::CreateFromNative(layout.release());
#else
    return nullptr;
#endif
}

std::shared_ptr<DescriptorPool> Device::createDescriptorPool(
    uint32_t maxSets, const std::vector<DescriptorPoolSizeDesc>& poolSizes) {
#ifdef TASROVY_API_VULKAN
    VulkanDescriptorPool::Builder builder(*impl_->context);
    for (const auto& poolSize : poolSizes) {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if (poolSize.type == DescriptorResourceType::CombinedImageSampler) {
            type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (poolSize.type == DescriptorResourceType::StorageImage) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        }
        builder.addPoolSize(type, poolSize.count);
    }
    builder.setMaxSets(maxSets);
    builder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    auto pool = builder.build();
    return DescriptorPool::CreateFromNative(pool.release());
#else
    return nullptr;
#endif
}

DescriptorSet Device::allocateDescriptorSet(DescriptorPool& pool, const DescriptorSetLayout& layout) {
    return pool.allocateSet(layout);
}

void Device::updateDescriptorSet(const DescriptorSet& descriptorSet, const std::vector<DescriptorWriteDesc>& writes) {
    updateDescriptorSet(descriptorSet.getNativeSet(), writes);
}

void Device::updateDescriptorSet(void* descriptorSet, const std::vector<DescriptorWriteDesc>& writes) {
#ifdef TASROVY_API_VULKAN
    DescriptorWriter writer(*impl_->context, static_cast<VkDescriptorSet>(descriptorSet));
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());

    for (const auto& write : writes) {
        if (write.buffer) {
            auto info = write.buffer->getDescriptorInfo();
            bufferInfos.push_back({
                reinterpret_cast<VkBuffer>(info.nativeBuffer),
                static_cast<VkDeviceSize>(info.offset),
                static_cast<VkDeviceSize>(info.range)
            });
            writer.writeBuffer(write.binding, &bufferInfos.back());
            continue;
        }

        if (write.image || write.imageInfo.nativeView != 0) {
            auto info = write.image ? write.image->getDescriptorInfo() : write.imageInfo;
            imageInfos.push_back({
                reinterpret_cast<VkSampler>(info.nativeSampler),
                reinterpret_cast<VkImageView>(info.nativeView),
                static_cast<VkImageLayout>(info.imageLayout)
            });
            auto descriptorType = write.type == DescriptorResourceType::StorageImage
                ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writer.writeImage(write.binding, &imageInfos.back(), descriptorType);
        }
    }

    writer.update();
#endif
}

void Device::createIBLMaps(Image& skybox, const std::string& name) {
#ifdef TASROVY_API_VULKAN
    if (!impl_->ibl) {
        impl_->ibl = std::make_unique<IBLProcessor>(*impl_->context, *impl_->submitter);
    }

    auto* skyboxImage = static_cast<VulkanImage*>(skybox.getNativeImageObject());
    if (skyboxImage) {
        impl_->ibl->addSkybox(*skyboxImage, name);
    }
#endif
}

DescriptorImageInfo Device::getIBLDescriptorInfo(IBLMapType mapType, const std::string& name) const {
#ifdef TASROVY_API_VULKAN
    if (!impl_->ibl) return {};

    VulkanImage* image = nullptr;
    switch (mapType) {
    case IBLMapType::Irradiance:
        image = impl_->ibl->getIrradianceMap(name);
        break;
    case IBLMapType::Prefiltered:
        image = impl_->ibl->getPrefilteredMap(name);
        break;
    case IBLMapType::BrdfLut:
        image = impl_->ibl->getBrdfLUT();
        break;
    }

    if (!image) return {};
    auto info = image->getDescriptorInfo();
    return {
        reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        static_cast<uint32_t>(info.imageLayout)
    };
#else
    return {};
#endif
}

// --- Upload ---

void Device::uploadBuffer(Buffer& dst, const void* data, uint64_t size) {
#ifdef TASROVY_API_VULKAN
    auto staging = Buffer::CreateStagingFromNative(impl_->context, size);
    staging->setData(data, size);
    impl_->submitter->submit([&](VkCommandBuffer cmd) {
        VkBuffer srcBuf = reinterpret_cast<VkBuffer>(staging->getNativeHandle());
        VkBuffer dstBuf = reinterpret_cast<VkBuffer>(dst.getNativeHandle());
        VkBufferCopy region{ 0, 0, size };
        vkCmdCopyBuffer(cmd, srcBuf, dstBuf, 1, &region);
    });
#endif
}

// --- Shader Reflection ---

std::shared_ptr<ReflectionBridge> Device::getReflectionBridge() {
    return impl_->reflectionBridge;
}

void Device::requestShaderReflection(Tasrovy::Render::Material* material) {
    if (!material || !impl_->reflectionBridge) return;
    auto vertexShader = material->getVertexShader();
    auto fragmentShader = material->getFragmentShader();
    if (!vertexShader && !fragmentShader) return;

    ReflectionRequest req;
    req.material = material;
    if (vertexShader) {
        req.vertSpvPath = vertexShader->getPath();
        req.vertEntryPoint = vertexShader->getEntry();
    }
    if (fragmentShader) {
        req.fragSpvPath = fragmentShader->getPath();
        req.fragEntryPoint = fragmentShader->getEntry();
    }

    impl_->reflectionBridge->submitRequest(std::move(req));
}

void Device::processReflectionResults() {
    if (impl_->reflectionBridge) {
        impl_->reflectionBridge->pollResults();
    }
}

// --- Frame lifecycle ---

void Device::waitIdle() {
#ifdef TASROVY_API_VULKAN
    if (impl_ && impl_->context) {
        if (impl_->renderer) {
            impl_->renderer->waitIdle();
        } else {
            vkDeviceWaitIdle(impl_->context->getDevice());
        }
    }
#endif
}

void Device::handleResize(Tasrovy::Windowing::Window& window) {
#ifdef TASROVY_API_VULKAN
    impl_->context->updateFramebufferSize(window.getWidth(), window.getHeight());
    impl_->context->framebufferResized = true;
#endif
}

void Device::checkSwapchain() {
#ifdef TASROVY_API_VULKAN
    impl_->context->CheckFormatChange(*impl_->swapchain);
#endif
}

uint32_t Device::getCurrentFrameIndex() const {
#ifdef TASROVY_API_VULKAN
    return impl_->renderer->getCurrentFrame();
#else
    return 0;
#endif
}

uint32_t Device::getSwapchainWidth() const {
#ifdef TASROVY_API_VULKAN
    return impl_->swapchain->getExtent().width;
#else
    return 0;
#endif
}

uint32_t Device::getSwapchainHeight() const {
#ifdef TASROVY_API_VULKAN
    return impl_->swapchain->getExtent().height;
#else
    return 0;
#endif
}

uint32_t Device::getSwapchainColorFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->swapchain->getImageFormat());
#else
    return 0;
#endif
}

uint32_t Device::getDepthFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->context->findDepthFormat());
#else
    return 0;
#endif
}

bool Device::beginFrame(CommandList& commandList) {
#ifdef TASROVY_API_VULKAN
    auto cmd = impl_->renderer->beginFrame(*impl_->swapchain);
    if (!cmd) return false;
    commandList.useNativeCommandBuffer(reinterpret_cast<uint64_t>(cmd));
    return true;
#else
    return false;
#endif
}

void Device::beginFrameRenderPass(CommandList& commandList) {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->beginRenderPass(
        reinterpret_cast<VkCommandBuffer>(commandList.getNativeCommandBuffer()),
        *impl_->swapchain);
#endif
}

void Device::endFrameRenderPass(CommandList& commandList) {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->endRenderPass(
        reinterpret_cast<VkCommandBuffer>(commandList.getNativeCommandBuffer()),
        *impl_->swapchain);
#endif
}

void Device::endFrame() {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->endFrame(*impl_->swapchain, *impl_->graphicsQueue, *impl_->presentQueue);
#endif
}

std::unique_ptr<Tasrovy::UI::UIOverlay> Device::createUIOverlay(Tasrovy::Windowing::Window& window) {
#ifdef TASROVY_API_VULKAN
    Tasrovy::UI::UIOverlay::CreateInfo info{};
    info.window = window.getHandle();
    info.instance = impl_->context->getInstance();
    info.physicalDevice = impl_->context->getPhysicalDevice();
    info.device = impl_->context->getDevice();
    info.graphicsQueue = impl_->graphicsQueue->getQueue();
    info.queueFamily = impl_->context->getQueueFamilyIndices().graphicsFamily.value();
    info.minImageCount = impl_->swapchain->getImageCount();
    // ImGui rotates through one vertex/index buffer pair per ImageCount. The
    // renderer may have more frames in flight than swapchain images, so using
    // only the swapchain count can make ImGui destroy a buffer that an older
    // frame command buffer still references.
    info.imageCount = std::max(
        impl_->swapchain->getImageCount(), impl_->renderer->getMaxFramesInFlight());
    info.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    info.colorFormat = impl_->swapchain->getImageFormat();
    return std::make_unique<Tasrovy::UI::UIOverlay>(info);
#else
    return nullptr;
#endif
}

bool Device::beginUIFrame(Tasrovy::UI::UIOverlay& ui) {
    return ui.beginFrame();
}

void Device::renderUI(Tasrovy::UI::UIOverlay& ui, CommandList& commandList) {
#ifdef TASROVY_API_VULKAN
    auto cmd = reinterpret_cast<VkCommandBuffer>(commandList.getNativeCommandBuffer());
    const auto imageIndex = impl_->renderer->getImageIndex();
    impl_->swapchain->recordLayoutTransition(
        cmd,
        imageIndex,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ui.endFrame(cmd, impl_->swapchain->getImageView(imageIndex), impl_->swapchain->getExtent());
    impl_->swapchain->recordLayoutTransition(
        cmd,
        imageIndex,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
#endif
}

} // namespace Tasrovy::RHI
