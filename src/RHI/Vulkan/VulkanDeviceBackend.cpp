#include "VulkanDeviceBackend.h"

#include "DescriptorWriter.h"
#include "IBLMap.h"
#include "ImmediateSubmitter.h"
#include "Renderer.h"
#include "VulkanBuffer.h"
#include "VulkanCommandListBackend.h"
#include "VulkanContext.h"
#include "VulkanConversions.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDescriptorSetLayout.h"
#include "VulkanFrameSchedulerBackend.h"
#include "VulkanImage.h"
#include "VulkanPipeline.h"
#include "VulkanQueue.h"
#include "VulkanResourceBackends.h"
#include "VulkanShaderBinary.h"
#include "VulkanSwapChain.h"
#ifdef TASROVY_ENABLE_DLSS_NR
#include "VulkanDlssNrExecutor.h"
#endif
#include "../RHIBackendAccess.h"

#include <GLFW/glfw3.h>
#include <Logger.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace Tasrovy::RHI::Vulkan {
namespace {

template <typename Range>
void appendUniqueExtensions(
    std::vector<std::string>& destination, const Range& source) {
    for (const auto& extension : source) {
        if (std::find(destination.begin(), destination.end(), extension) ==
            destination.end()) {
            destination.emplace_back(extension);
        }
    }
}

VkDescriptorType descriptorType(DescriptorResourceType type) {
    switch (type) {
    case DescriptorResourceType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorResourceType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case DescriptorResourceType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case DescriptorResourceType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

void requireFormatFeatures(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatFeatureFlags required,
    const char* usageName) {
    if (format == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument("Cannot create an image with an unknown format");
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    if ((properties.optimalTilingFeatures & required) != required) {
        throw std::runtime_error(
            std::string("Vulkan format ") + std::to_string(format) +
            " does not support " + usageName + " with optimal tiling");
    }
}

} // namespace

VulkanDeviceBackend::VulkanDeviceBackend(
    const SurfaceDeviceCreateInfo& createInfo) {
    uint32_t extensionCount = 0;
    const char** extensionNames =
        glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensionNames || extensionCount == 0)
        throw std::runtime_error("GLFW did not provide Vulkan extensions");
    std::vector<const char*> extensions(
        extensionNames, extensionNames + extensionCount);
    std::vector<std::string> deviceExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };
#if defined(TASROVY_ENABLE_DLSS_NR) && \
    TASROVY_ALLOW_UNTRUSTED_DLSS_NR_RUNTIME
    const auto ngxExtensions = queryDlssNrExtensionRequirements();
    std::vector<std::string> instanceExtensionStorage;
    instanceExtensionStorage.reserve(ngxExtensions.instance.size());
    appendUniqueExtensions(instanceExtensionStorage, ngxExtensions.instance);
    for (const auto& extension : instanceExtensionStorage) {
        const bool alreadyPresent = std::any_of(
            extensions.begin(), extensions.end(),
            [&extension](const char* existing) {
                return extension == existing;
            });
        if (!alreadyPresent)
            extensions.push_back(extension.c_str());
    }
    appendUniqueExtensions(deviceExtensions, ngxExtensions.device);
#endif
    auto* window = static_cast<GLFWwindow*>(createInfo.nativeWindowHandle);
    context_ = std::make_unique<VulkanContext>(
        "Vulkan", extensions, std::move(deviceExtensions),
        [window](VkInstance instance) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(
                    instance, window, nullptr, &surface) != VK_SUCCESS)
                throw std::runtime_error("Failed to create Vulkan surface");
            return surface;
        },
        static_cast<int>(createInfo.framebufferWidth),
        static_cast<int>(createInfo.framebufferHeight));
    context_->updateFramebufferSize(
        static_cast<int>(createInfo.framebufferWidth),
        static_cast<int>(createInfo.framebufferHeight));
    graphicsQueue_ = std::make_unique<VulkanQueue>(
        *context_, QueueType::Graphics);
    presentQueue_ = std::make_unique<VulkanQueue>(
        *context_, QueueType::Present);
    renderer_ = std::make_unique<Renderer>(
        *context_, createInfo.maxFramesInFlight);
    submitter_ = std::make_unique<ImmediateSubmitter>(
        *context_, *graphicsQueue_);
    swapchain_ = std::make_unique<VulkanSwapchain>(*context_);
    renderer_->onSwapchainRecreated(swapchain_->getImageCount());
}

VulkanDeviceBackend::~VulkanDeviceBackend() {
    // Frame fences cover graphics submissions, but not the presentation
    // operation queued after each submission. Waiting for the complete device
    // here guarantees that the presentation engine has released the swapchain
    // images and render-finished semaphores before their owners are destroyed.
    if (renderer_) {
        try {
            renderer_->waitIdle();
        } catch (const std::exception& error) {
            LOG_ERROR(
                "Shutdown: renderer fence drain failed before Vulkan teardown: {}",
                error.what());
        }
    }
    if (context_) {
        context_->waitIdleForShutdown();
    }

    // Do not rely on reverse member-declaration order for Vulkan dependencies.
    // All GPU work is idle now, so destroy API objects explicitly while their
    // context and queues are still alive.
    ibl_.reset();
    swapchain_.reset();
    submitter_.reset();
    renderer_.reset();
    presentQueue_.reset();
    graphicsQueue_.reset();
    context_.reset();
}

void VulkanDeviceBackend::waitIdleForShutdown() {
    if (context_) context_->waitIdleForShutdown();
}

std::unique_ptr<IFrameSchedulerBackend>
VulkanDeviceBackend::createFrameScheduler() {
    if (schedulerCreated_)
        throw std::logic_error("Vulkan frame scheduler already created");
    schedulerCreated_ = true;
    return std::make_unique<VulkanFrameSchedulerBackend>(
        *context_, *renderer_, *swapchain_, *graphicsQueue_,
        *presentQueue_, *submitter_);
}

std::unique_ptr<ICommandListBackend>
VulkanDeviceBackend::createCommandList() {
    return std::make_unique<VulkanCommandListBackend>(*context_);
}

std::unique_ptr<IBufferBackend> VulkanDeviceBackend::createBuffer(
    const BufferDesc& desc) {
    const VkMemoryPropertyFlags properties = desc.hostVisible
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return std::make_unique<VulkanBufferBackend>(
        std::make_unique<VulkanBuffer>(
            *context_, desc.size, toVkBufferUsage(desc.usage), properties,
            desc.debugName));
}

std::unique_ptr<IImageBackend> VulkanDeviceBackend::createTexture(
    const ImageUploadDesc& upload) {
    const VkFormat format = upload.format == Format::Unknown
        ? VK_FORMAT_R8G8B8A8_SRGB : toVkFormat(upload.format);
    if (upload.format != Format::Unknown && isDepthFormat(upload.format)) {
        throw std::invalid_argument(
            "Texture uploads do not accept depth/stencil formats");
    }
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (upload.generateMipmaps) {
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    requireFormatFeatures(
        context_->getPhysicalDevice(), format, required,
        "sampled texture upload usage");
    auto image = upload.cubemap
        ? VulkanImage::createCubemap(
            *context_, *submitter_, upload.pixels.data(),
            upload.pixels.size() / std::max(upload.arrayLayers, 1u),
            upload.width, upload.height, format)
        : VulkanImage::createTexture(
            *context_, *submitter_, upload.pixels.data(),
            upload.pixels.size(), upload.width, upload.height,
            upload.generateMipmaps, format);
    if (image) {
        LOG_GPU_MEMORY(
            "[LABEL] type=Image image=0x{:x} name='{}' source=TextureUpload "
            "extent={}x{} format={} mipLevels={} layers={}",
            reinterpret_cast<uintptr_t>(image->getImage()),
            upload.debugName.empty() ? "UnnamedTexture" : upload.debugName,
            upload.width,
            upload.height,
            static_cast<int32_t>(format),
            image->getMipLevels(),
            upload.cubemap ? 6u : 1u);
    }
    return std::make_unique<VulkanImageBackend>(std::move(image));
}

std::unique_ptr<IImageBackend> VulkanDeviceBackend::createSolidTexture(
    const std::array<float, 4>& color, Format format) {
    if (isDepthFormat(format)) {
        throw std::invalid_argument(
            "Solid color textures do not accept depth/stencil formats");
    }
    const auto vkFormat = toVkFormat(format);
    requireFormatFeatures(
        context_->getPhysicalDevice(), vkFormat,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
        "solid sampled texture usage");
    return std::make_unique<VulkanImageBackend>(
        VulkanImage::createSolidTexture(
            *context_, *submitter_, color, vkFormat));
}

std::unique_ptr<IImageBackend> VulkanDeviceBackend::createAttachment(
    uint32_t width, uint32_t height, Format format,
    bool storage, bool useDeviceMsaa) {
    const auto vkFormat = toVkFormat(format);
    const bool depth = isDepthFormat(format);
    if (storage && depth) {
        throw std::invalid_argument(
            "Depth/stencil render textures cannot be storage images");
    }
    VkImageUsageFlags usage = depth
        ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT
        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT;
    if (storage && !depth) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        (depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
               : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    if (storage && !depth) {
        required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    requireFormatFeatures(
        context_->getPhysicalDevice(), vkFormat, required,
        storage ? "sampled, attachment and storage image usage"
                : "sampled and attachment image usage");
    return std::make_unique<VulkanImageBackend>(
        VulkanImage::createAttachment(
            *context_, {width, height}, vkFormat, usage,
            useDeviceMsaa ? context_->getMsaaSamples()
                          : VK_SAMPLE_COUNT_1_BIT));
}

std::unique_ptr<IImageBackend> VulkanDeviceBackend::createImage2D(
    uint32_t width, uint32_t height, Format format) {
    if (isDepthFormat(format)) {
        throw std::invalid_argument(
            "Sampled image creation does not accept depth/stencil formats");
    }
    const auto vkFormat = toVkFormat(format);
    requireFormatFeatures(
        context_->getPhysicalDevice(), vkFormat,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
        "sampled image usage");
    return std::make_unique<VulkanImageBackend>(
        VulkanImage::createImage2D(
            *context_, {width, height}, vkFormat,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
}

std::unique_ptr<IImageBackend> VulkanDeviceBackend::createVirtualShadowMap(
    const VirtualShadowMapDesc& desc) {
    if (!isDepthFormat(desc.format)) {
        throw std::invalid_argument(
            "Virtual shadow maps require a depth format");
    }
    const auto vkFormat = toVkFormat(desc.format);
    requireFormatFeatures(
        context_->getPhysicalDevice(), vkFormat,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        "sampled depth attachment usage");
    return std::make_unique<VulkanImageBackend>(
        VulkanImage::createVirtualShadowAtlas(
            *context_, {desc.atlasSize, desc.atlasSize},
            vkFormat));
}

std::unique_ptr<IPipelineBackend>
VulkanDeviceBackend::createGraphicsPipeline(const PipelineDesc& desc) {
    PipelineBuilder builder(*context_);
    builder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT,
        resolveShaderBinary(desc.vertexShader),
        desc.vertexShader.entryPoint.c_str());
    builder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT,
        resolveShaderBinary(desc.fragmentShader),
        desc.fragmentShader.entryPoint.c_str());
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = desc.vertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (size_t index = 0; index < desc.attributeLocations.size(); ++index) {
        attributes.push_back({desc.attributeLocations[index], 0,
            toVkFormat(desc.attributeFormats[index]),
            desc.attributeOffsets[index]});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = desc.vertexStride == 0 ? 0 : 1;
    vertexInput.pVertexBindingDescriptions = desc.vertexStride == 0
        ? nullptr : &binding;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    builder.setVertexInputState(vertexInput);
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = toVkTopology(desc.topology);
    builder.setInputAssemblyState(assembly);
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depthTest;
    depth.depthWriteEnable = desc.depthWrite;
    depth.depthCompareOp = toVkCompareOp(desc.depthCompareOp);
    builder.setDepthStencilState(depth);
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = desc.useMSAA
        ? context_->getMsaaSamples() : VK_SAMPLE_COUNT_1_BIT;
    builder.setMultisampleState(multisample);
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = toVkCullMode(desc.cullMode);
    raster.frontFace = toVkFrontFace(desc.frontFace);
    builder.setRasterizationState(raster);
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = desc.blendMode != BlendMode::Off;
    blendAttachment.srcColorBlendFactor = desc.blendMode == BlendMode::Alpha
        ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = desc.blendMode == BlendMode::Alpha
        ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
        : desc.blendMode == BlendMode::Additive
            ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = blendAttachment.dstColorBlendFactor;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    builder.setColorBlendState(blend);
    if (desc.descriptorSetLayout) {
        builder.addDescriptorSetLayout(reinterpret_cast<VkDescriptorSetLayout>(
            BackendAccess::descriptorSetLayout(*desc.descriptorSetLayout)));
    }
    std::vector<VkFormat> colorFormats;
    for (const auto format : desc.colorAttachmentFormats)
        colorFormats.push_back(toVkFormat(format));
    auto depthFormat = desc.depthAttachmentFormat;
    if (depthFormat == Format::Unknown && (desc.depthTest || desc.depthWrite))
        depthFormat = fromVkFormat(context_->findDepthFormat());
    builder.setRenderingFormats(colorFormats, toVkFormat(depthFormat));
    return std::make_unique<VulkanPipelineBackend>(
        builder.buildGraphicsPipeline());
}

std::unique_ptr<IPipelineBackend>
VulkanDeviceBackend::createComputePipeline(const ComputePipelineDesc& desc) {
    if (!desc.descriptorSetLayout)
        throw std::invalid_argument("Compute pipeline requires a layout");
    PipelineBuilder builder(*context_);
    return std::make_unique<VulkanPipelineBackend>(
        builder.buildComputePipeline(resolveShaderBinary(desc.shader),
            reinterpret_cast<VkDescriptorSetLayout>(
                BackendAccess::descriptorSetLayout(
                    *desc.descriptorSetLayout)),
            desc.shader.entryPoint.c_str()));
}

std::unique_ptr<IDescriptorSetLayoutBackend>
VulkanDeviceBackend::createDescriptorSetLayout(
    const DescriptorSetDesc& desc) {
    VulkanDescriptorSetLayout::Builder builder(*context_);
    for (uint32_t binding = 0; binding < desc.bindingTypes.size(); ++binding) {
        builder.addBinding(binding, descriptorType(desc.bindingTypes[binding]),
            binding < desc.stageFlags.size()
                ? toVkShaderStages(desc.stageFlags[binding])
                : VK_SHADER_STAGE_ALL);
    }
    return std::make_unique<VulkanDescriptorSetLayoutBackend>(builder.build());
}

std::unique_ptr<IDescriptorPoolBackend>
VulkanDeviceBackend::createDescriptorPool(
    uint32_t maxSets,
    const std::vector<DescriptorPoolSizeDesc>& poolSizes) {
    VulkanDescriptorPool::Builder builder(*context_);
    for (const auto& size : poolSizes)
        builder.addPoolSize(descriptorType(size.type), size.count);
    builder.setMaxSets(maxSets);
    builder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    return std::make_unique<VulkanDescriptorPoolBackend>(builder.build());
}

void VulkanDeviceBackend::updateDescriptorSet(
    const IDescriptorSetBackend& descriptorSet,
    const std::vector<DescriptorWriteDesc>& writes) {
    DescriptorWriter writer(
        *context_, reinterpret_cast<VkDescriptorSet>(descriptorSet.nativeSet()));
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;
    buffers.reserve(writes.size());
    images.reserve(writes.size());
    for (const auto& write : writes) {
        if (write.buffer) {
            const auto info = BackendAccess::descriptorInfo(*write.buffer);
            buffers.push_back({reinterpret_cast<VkBuffer>(info.backendBuffer),
                info.offset, info.range});
            writer.writeBuffer(write.binding, &buffers.back(),
                descriptorType(write.type));
        } else if (write.image || write.imageInfo.backendView != 0) {
            const auto info = write.image
                ? BackendAccess::descriptorInfo(*write.image) : write.imageInfo;
            images.push_back({
                reinterpret_cast<VkSampler>(info.backendSampler),
                reinterpret_cast<VkImageView>(info.backendView),
                toVkImageLayout(info.imageLayout)});
            writer.writeImage(write.binding, &images.back(),
                descriptorType(write.type));
        }
    }
    writer.update();
}

void VulkanDeviceBackend::createIBLMaps(
    IImageBackend& skybox, const std::string& name) {
    if (!ibl_) ibl_ = std::make_unique<IBLProcessor>(*context_, *submitter_);
    auto* image = dynamic_cast<VulkanImageBackend*>(&skybox);
    if (!image) throw std::invalid_argument("Image backend mismatch");
    ibl_->addSkybox(image->value(), name);
}

DescriptorImageInfo VulkanDeviceBackend::getIBLDescriptorInfo(
    IBLMapType mapType, const std::string& name) const {
    if (!ibl_) return {};
    VulkanImage* image = nullptr;
    switch (mapType) {
    case IBLMapType::Irradiance: image = ibl_->getIrradianceMap(name); break;
    case IBLMapType::Prefiltered: image = ibl_->getPrefilteredMap(name); break;
    case IBLMapType::BrdfLut: image = ibl_->getBrdfLUT(); break;
    }
    if (!image) return {};
    const auto info = image->getDescriptorInfo();
    return {reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        fromVkImageLayout(info.imageLayout)};
}

Format VulkanDeviceBackend::depthFormat() const {
    return fromVkFormat(context_->findDepthFormat());
}
size_t VulkanDeviceBackend::deferredDeletionCount() const {
    return context_->getDeferredDeletionCount();
}
BackendInteropContext VulkanDeviceBackend::interopContext() const {
    BackendInteropContext info{};
    info.api = GraphicsAPI::Vulkan;
    info.handles[0] = reinterpret_cast<uintptr_t>(context_->getInstance());
    info.handles[1] = reinterpret_cast<uintptr_t>(context_->getPhysicalDevice());
    info.handles[2] = reinterpret_cast<uintptr_t>(context_->getDevice());
    info.handles[3] = reinterpret_cast<uintptr_t>(graphicsQueue_->getQueue());
    info.queueIndex = context_->getQueueFamilyIndices().graphicsFamily.value();
    info.minImageCount = swapchain_->getImageCount();
    info.imageCount = std::max(
        swapchain_->getImageCount(), renderer_->getMaxFramesInFlight());
    info.sampleCount = static_cast<uint32_t>(VK_SAMPLE_COUNT_1_BIT);
    info.presentationFormat = fromVkFormat(swapchain_->getImageFormat());
    return info;
}

} // namespace Tasrovy::RHI::Vulkan
