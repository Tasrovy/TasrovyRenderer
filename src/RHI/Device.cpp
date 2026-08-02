#include "Device.h"
#include "FrameScheduler.h"
#include "Buffer.h"
#include "Image.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "CommandList.h"
#include "RHIConfig.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
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
    #include <GLFW/glfw3.h>
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
    std::unique_ptr<FrameScheduler> frameScheduler;
    std::mutex resourceScopeMutex;
    ResourceScope nextResourceScope = 1;
    std::unordered_map<ResourceScope, std::vector<std::shared_ptr<void>>> resourceScopes;
};

std::shared_ptr<Device> Device::create(void* nativeContext, void* nativeSubmitter) {
    auto dev = std::shared_ptr<Device>(new Device());
    dev->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    dev->impl_->context = static_cast<VulkanContext*>(nativeContext);
    dev->impl_->submitter = static_cast<ImmediateSubmitter*>(nativeSubmitter);
#endif
    return dev;
}

std::shared_ptr<Device> Device::createForSurface(
    const SurfaceDeviceCreateInfo& createInfo) {
    auto dev = std::shared_ptr<Device>(new Device());
    dev->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    uint32_t extensionCount = 0;
    const char** extensionNames =
        glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensionNames || extensionCount == 0) {
        throw std::runtime_error(
            "GLFW did not provide Vulkan instance extensions");
    }
    std::vector<const char*> instanceExtensions(
        extensionNames,
        extensionNames + extensionCount);
    auto* nativeWindow =
        static_cast<GLFWwindow*>(createInfo.nativeWindowHandle);
    dev->impl_->ownedContext = std::make_unique<VulkanContext>(
        "Vulkan",
        instanceExtensions,
        [nativeWindow](VkInstance instance) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(
                    instance,
                    nativeWindow,
                    nullptr,
                    &surface) != VK_SUCCESS) {
                throw std::runtime_error(
                    "Failed to create GLFW Vulkan surface");
            }
            return surface;
        },
        static_cast<int>(createInfo.framebufferWidth),
        static_cast<int>(createInfo.framebufferHeight));
    dev->impl_->context = dev->impl_->ownedContext.get();
    dev->impl_->context->updateFramebufferSize(
        static_cast<int>(createInfo.framebufferWidth),
        static_cast<int>(createInfo.framebufferHeight));
    dev->impl_->graphicsQueue = std::make_unique<VulkanQueue>(*dev->impl_->context, QueueType::Graphics);
    dev->impl_->presentQueue = std::make_unique<VulkanQueue>(*dev->impl_->context, QueueType::Present);
    dev->impl_->renderer = std::make_unique<Renderer>(
        *dev->impl_->context,
        createInfo.maxFramesInFlight);
    dev->impl_->ownedSubmitter = std::make_unique<ImmediateSubmitter>(*dev->impl_->context, *dev->impl_->graphicsQueue);
    dev->impl_->submitter = dev->impl_->ownedSubmitter.get();
    dev->impl_->swapchain = std::make_unique<VulkanSwapchain>(*dev->impl_->context);
    dev->impl_->renderer->onSwapchainRecreated(dev->impl_->swapchain->getImageCount());
    dev->impl_->frameScheduler = FrameScheduler::create(
        dev->impl_->context,
        dev->impl_->renderer.get(),
        dev->impl_->swapchain.get(),
        dev->impl_->graphicsQueue.get(),
        dev->impl_->presentQueue.get(),
        dev->impl_->submitter);
#endif
    return dev;
}

Device::~Device() {
    std::unordered_map<ResourceScope, std::vector<std::shared_ptr<void>>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        retired.swap(impl_->resourceScopes);
    }
    retired.clear();
#ifdef TASROVY_API_VULKAN
    if (impl_->frameScheduler) {
        impl_->frameScheduler->waitForInFlightFrames();
    } else if (impl_->context) {
        vkDeviceWaitIdle(impl_->context->getDevice());
    }
#endif
}

Device::ResourceScope Device::createResourceScope() {
    std::lock_guard lock(impl_->resourceScopeMutex);
    const ResourceScope scope = impl_->nextResourceScope++;
    impl_->resourceScopes.try_emplace(scope);
    return scope;
}

void Device::retainResourceUntyped(
    ResourceScope scope,
    std::shared_ptr<void> resource) {
    if (!resource) {
        return;
    }
    std::lock_guard lock(impl_->resourceScopeMutex);
    impl_->resourceScopes[scope].push_back(std::move(resource));
}

void Device::resetResourceScope(ResourceScope scope) {
    std::vector<std::shared_ptr<void>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        const auto found = impl_->resourceScopes.find(scope);
        if (found == impl_->resourceScopes.end()) {
            return;
        }
        retired.swap(found->second);
    }
    retired.clear();
}

void Device::destroyResourceScope(ResourceScope scope) {
    std::vector<std::shared_ptr<void>> retired;
    {
        std::lock_guard lock(impl_->resourceScopeMutex);
        const auto found = impl_->resourceScopes.find(scope);
        if (found == impl_->resourceScopes.end()) {
            return;
        }
        retired = std::move(found->second);
        impl_->resourceScopes.erase(found);
    }
    retired.clear();
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

std::shared_ptr<CommandList> Device::createCommandList() {
    auto commandList = CommandList::create();
#ifdef TASROVY_API_VULKAN
    commandList->setBackendContext(impl_->context);
#endif
    return commandList;
}

// --- Image creation ---

std::shared_ptr<Image> Device::createTexture(
    const ImageUploadDesc& upload) {
    return Image::CreateTextureFromNative(
        impl_->context, impl_->submitter, upload);
}

std::shared_ptr<Image> Device::createSolidTexture(
    const std::array<float, 4>& color,
    uint32_t format) {
    return Image::CreateSolidTextureFromNative(
        impl_->context, impl_->submitter, color, format);
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
        1,
        desc.storage);
}

std::shared_ptr<Image> Device::createVirtualShadowMap(
    const VirtualShadowMapDesc& desc) {
    if (desc.atlasSize == 0 ||
        desc.pageSize == 0 ||
        desc.atlasSize % desc.pageSize != 0) {
        return nullptr;
    }
    const uint32_t pagesPerAxis = desc.atlasSize / desc.pageSize;
    if (desc.residentPageCount == 0 ||
        desc.residentPageCount > pagesPerAxis * pagesPerAxis) {
        return nullptr;
    }
    return Image::CreateVirtualShadowAtlasFromNative(
        impl_->context,
        desc.atlasSize,
        desc.format);
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
        return impl_->frameScheduler
            ? impl_->frameScheduler->getColorFormat()
            : FormatRGBA8Unorm;
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

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable =
        desc.blendMode == BlendOff ? VK_FALSE : VK_TRUE;
    blendAttachment.srcColorBlendFactor =
        desc.blendMode == BlendAlpha
            ? VK_BLEND_FACTOR_SRC_ALPHA
            : VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor =
        desc.blendMode == BlendAlpha
            ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
            : (desc.blendMode == BlendAdditive
                ? VK_BLEND_FACTOR_ONE
                : VK_BLEND_FACTOR_ZERO);
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor =
        desc.blendMode == BlendAlpha
            ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
            : (desc.blendMode == BlendAdditive
                ? VK_BLEND_FACTOR_ONE
                : VK_BLEND_FACTOR_ZERO);
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    builder.setColorBlendState(blend);

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

std::shared_ptr<Pipeline> Device::createComputePipeline(
    const ComputePipelineDesc& desc) {
#ifdef TASROVY_API_VULKAN
    if (!desc.descriptorSetLayout) {
        throw std::runtime_error("compute pipeline requires a descriptor set layout");
    }
    PipelineBuilder builder(*impl_->context);
    auto pipeline = builder.buildComputePipeline(
        desc.shaderPath,
        reinterpret_cast<VkDescriptorSetLayout>(
            desc.descriptorSetLayout->getNativeLayout()),
        desc.entryPoint.c_str());
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
        } else if (desc.bindingTypes[i] == DescriptorResourceType::StorageBuffer) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
        } else if (poolSize.type == DescriptorResourceType::StorageBuffer) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
            writer.writeBuffer(
                write.binding,
                &bufferInfos.back(),
                write.type == DescriptorResourceType::StorageBuffer
                    ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
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

// --- Frame lifecycle ---

FrameScheduler& Device::getFrameScheduler() {
    return *impl_->frameScheduler;
}

const FrameScheduler& Device::getFrameScheduler() const {
    return *impl_->frameScheduler;
}

uint32_t Device::getDepthFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->context->findDepthFormat());
#else
    return 0;
#endif
}

size_t Device::getDeferredDeletionCount() const {
#ifdef TASROVY_API_VULKAN
    return impl_ && impl_->context ? impl_->context->getDeferredDeletionCount() : 0;
#else
    return 0;
#endif
}


NativeOverlayContext Device::getNativeOverlayContext() const {
#ifdef TASROVY_API_VULKAN
    NativeOverlayContext info{};
    info.instance = reinterpret_cast<uint64_t>(
        impl_->context->getInstance());
    info.physicalDevice = reinterpret_cast<uint64_t>(
        impl_->context->getPhysicalDevice());
    info.device = reinterpret_cast<uint64_t>(
        impl_->context->getDevice());
    info.graphicsQueue = reinterpret_cast<uint64_t>(
        impl_->graphicsQueue->getQueue());
    info.queueFamily = impl_->context->getQueueFamilyIndices().graphicsFamily.value();
    info.minImageCount = impl_->swapchain->getImageCount();
    // ImGui rotates through one vertex/index buffer pair per ImageCount. The
    // renderer may have more frames in flight than swapchain images, so using
    // only the swapchain count can make ImGui destroy a buffer that an older
    // frame command buffer still references.
    info.imageCount = std::max(
        impl_->swapchain->getImageCount(), impl_->renderer->getMaxFramesInFlight());
    info.sampleCount = static_cast<uint32_t>(VK_SAMPLE_COUNT_1_BIT);
    info.colorFormat =
        static_cast<uint32_t>(impl_->swapchain->getImageFormat());
    return info;
#else
    return {};
#endif
}


} // namespace Tasrovy::RHI
