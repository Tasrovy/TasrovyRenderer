#include "CommandList.h"
#include "Buffer.h"
#include "Image.h"
#include "Pass.h"
#include "Descriptor.h"
#include "RHIConfig.h"
#include <vector>

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/VulkanBuffer.h"
#endif

namespace Tasrovy::RHI {

struct CommandList::Impl {
#ifdef TASROVY_API_VULKAN
    VulkanContext* context = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer activeCmdBuffer = VK_NULL_HANDLE;
    VkPipelineLayout boundPipelineLayout = VK_NULL_HANDLE;
    bool isRecording = false;
#endif
};

std::shared_ptr<CommandList> CommandList::create() {
    auto cmd = std::shared_ptr<CommandList>(new CommandList());
    cmd->impl_ = std::make_unique<Impl>();
    return cmd;
}

CommandList::~CommandList() {
#ifdef TASROVY_API_VULKAN
    if (impl_->commandPool) {
        vkDestroyCommandPool(impl_->context->getDevice(), impl_->commandPool, nullptr);
    }
#endif
}

void CommandList::setBackendContext(void* nativeContext) {
#ifdef TASROVY_API_VULKAN
    impl_->context = static_cast<VulkanContext*>(nativeContext);
#endif
}

void CommandList::useNativeCommandBuffer(uint64_t nativeCommandBuffer) {
#ifdef TASROVY_API_VULKAN
    impl_->activeCmdBuffer = reinterpret_cast<VkCommandBuffer>(nativeCommandBuffer);
    impl_->isRecording = nativeCommandBuffer != 0;
#endif
}

// --- Lifecycle ---

void CommandList::begin() {
#ifdef TASROVY_API_VULKAN
    auto* ctx = impl_->context;
    if (!impl_->commandPool) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = ctx->getQueueFamilyIndices().graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(ctx->getDevice(), &poolInfo, nullptr, &impl_->commandPool);
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = impl_->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(ctx->getDevice(), &allocInfo, &impl_->activeCmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(impl_->activeCmdBuffer, &beginInfo);
    impl_->isRecording = true;
#endif
}

void CommandList::end() {
#ifdef TASROVY_API_VULKAN
    if (impl_->isRecording) {
        vkEndCommandBuffer(impl_->activeCmdBuffer);
        impl_->isRecording = false;
    }
#endif
}

uint64_t CommandList::getNativeCommandBuffer() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->activeCmdBuffer);
#else
    return 0;
#endif
}

// --- Render pass ---

namespace {

#ifdef TASROVY_API_VULKAN
VkAttachmentLoadOp toVkLoadOp(RHIAttachmentLoad load) {
    switch (load) {
    case RHIAttachmentLoad::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RHIAttachmentLoad::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RHIAttachmentLoad::Discard:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

VkAttachmentStoreOp toVkStoreOp(RHIAttachmentStore store) {
    switch (store) {
    case RHIAttachmentStore::Store:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case RHIAttachmentStore::Discard:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_STORE;
}
#endif

} // namespace

void CommandList::beginRenderPass(Pass& pass) {
#ifdef TASROVY_API_VULKAN
    const auto& desc = pass.getDesc();
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    colorAttachments.reserve(desc.colorAttachments.size());

    for (const auto& attachment : desc.colorAttachments) {
        if (!attachment.image) {
            continue;
        }

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = reinterpret_cast<VkImageView>(attachment.image->getNativeView());
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = toVkLoadOp(attachment.load);
        colorAtt.storeOp = toVkStoreOp(attachment.store);
        colorAtt.clearValue.color.float32[0] = attachment.clearColor.x;
        colorAtt.clearValue.color.float32[1] = attachment.clearColor.y;
        colorAtt.clearValue.color.float32[2] = attachment.clearColor.z;
        colorAtt.clearValue.color.float32[3] = attachment.clearColor.w;
        colorAttachments.push_back(colorAtt);
    }

    VkRenderingAttachmentInfo depthAtt{};
    const bool hasDepth = desc.depthAttachment && desc.depthAttachment->image;
    if (hasDepth) {
        const auto& attachment = *desc.depthAttachment;
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = reinterpret_cast<VkImageView>(attachment.image->getNativeView());
        depthAtt.imageLayout = attachment.readOnly
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = toVkLoadOp(attachment.load);
        depthAtt.storeOp = toVkStoreOp(attachment.store);
        depthAtt.clearValue.depthStencil.depth = attachment.clearDepth;
        depthAtt.clearValue.depthStencil.stencil = 0;
    }

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { {0, 0}, {desc.width, desc.height} };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderInfo.pColorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data();
    renderInfo.pDepthAttachment = hasDepth ? &depthAtt : nullptr;
    renderInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(impl_->activeCmdBuffer, &renderInfo);
#endif
}

void CommandList::beginRenderPass(Pass& pass,
    uint64_t colorView, uint64_t resolveView, uint64_t depthView,
    uint32_t width, uint32_t height)
{
#ifdef TASROVY_API_VULKAN
    VkCommandBuffer cmd = impl_->activeCmdBuffer;

    // Color attachment
    VkRenderingAttachmentInfo colorAtt{};
    colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAtt.imageView = reinterpret_cast<VkImageView>(colorView);
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.clearValue.color.float32[0] = 0.0f;
    colorAtt.clearValue.color.float32[1] = 0.0f;
    colorAtt.clearValue.color.float32[2] = 0.0f;
    colorAtt.clearValue.color.float32[3] = 1.0f;

    // Resolve attachment (for MSAA) â€?set on color attachment, not VkRenderingInfo
    if (resolveView != 0) {
        colorAtt.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAtt.resolveImageView = reinterpret_cast<VkImageView>(resolveView);
        colorAtt.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Depth attachment
    VkRenderingAttachmentInfo depthAtt{};
    depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAtt.imageView = reinterpret_cast<VkImageView>(depthView);
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue.depthStencil.depth = 1.0f;
    depthAtt.clearValue.depthStencil.stencil = 0;

    // MSAA resolve via color attachment resolve fields
    if (resolveView != 0) {
        colorAtt.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAtt.resolveImageView = reinterpret_cast<VkImageView>(resolveView);
        colorAtt.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { {0, 0}, {width, height} };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAtt;
    renderInfo.pDepthAttachment = depthView != 0 ? &depthAtt : nullptr;
    renderInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd, &renderInfo);
#endif
}

void CommandList::endRenderPass() {
#ifdef TASROVY_API_VULKAN
    vkCmdEndRendering(impl_->activeCmdBuffer);
#endif
}

// --- Pipeline ---

void CommandList::bindPipeline(uint64_t nativePipeline, uint64_t nativeLayout, uint32_t bindPoint) {
#ifdef TASROVY_API_VULKAN
    impl_->boundPipelineLayout = reinterpret_cast<VkPipelineLayout>(nativeLayout);
    vkCmdBindPipeline(impl_->activeCmdBuffer,
        bindPoint == 0 ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
        reinterpret_cast<VkPipeline>(nativePipeline));
#endif
}

// --- Buffers ---

void CommandList::bindVertexBuffer(Buffer& buffer) {
#ifdef TASROVY_API_VULKAN
    VkBuffer vkBuf = reinterpret_cast<VkBuffer>(buffer.getNativeHandle());
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(impl_->activeCmdBuffer, 0, 1, &vkBuf, &offset);
#endif
}

void CommandList::bindIndexBuffer(Buffer& buffer) {
#ifdef TASROVY_API_VULKAN
    vkCmdBindIndexBuffer(impl_->activeCmdBuffer,
        reinterpret_cast<VkBuffer>(buffer.getNativeHandle()), 0, VK_INDEX_TYPE_UINT32);
#endif
}

// --- Descriptors ---

void CommandList::setPipelineLayout(uint64_t nativeLayout) {
#ifdef TASROVY_API_VULKAN
    impl_->boundPipelineLayout = reinterpret_cast<VkPipelineLayout>(nativeLayout);
#endif
}

void CommandList::bindDescriptorSet(uint32_t setIndex, void* descriptorSet) {
#ifdef TASROVY_API_VULKAN
    auto ds = static_cast<VkDescriptorSet>(descriptorSet);
    vkCmdBindDescriptorSets(impl_->activeCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        impl_->boundPipelineLayout, setIndex, 1, &ds, 0, nullptr);
#endif
}

void CommandList::bindDescriptorSet(uint32_t setIndex, const DescriptorSet& descriptorSet) {
    bindDescriptorSet(setIndex, descriptorSet.getNativeSet());
}

// --- State ---

void CommandList::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
#ifdef TASROVY_API_VULKAN
    VkViewport vp{ x, y, width, height, minDepth, maxDepth };
    vkCmdSetViewport(impl_->activeCmdBuffer, 0, 1, &vp);
#endif
}

void CommandList::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
#ifdef TASROVY_API_VULKAN
    VkRect2D sc{ {x, y}, {width, height} };
    vkCmdSetScissor(impl_->activeCmdBuffer, 0, 1, &sc);
#endif
}

// --- Draw ---

void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount) {
#ifdef TASROVY_API_VULKAN
    vkCmdDraw(impl_->activeCmdBuffer, vertexCount, instanceCount, 0, 0);
#endif
}

void CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount) {
#ifdef TASROVY_API_VULKAN
    vkCmdDrawIndexed(impl_->activeCmdBuffer, indexCount, instanceCount, 0, 0, 0);
#endif
}

// --- Copy ---

void CommandList::copyBuffer(Buffer& src, Buffer& dst, uint64_t size) {
#ifdef TASROVY_API_VULKAN
    VkBuffer srcBuf = reinterpret_cast<VkBuffer>(src.getNativeHandle());
    VkBuffer dstBuf = reinterpret_cast<VkBuffer>(dst.getNativeHandle());
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(impl_->activeCmdBuffer, srcBuf, dstBuf, 1, &region);
#endif
}

// --- Barrier ---

void CommandList::pipelineBarrier(uint32_t srcStage, uint32_t dstStage) {
#ifdef TASROVY_API_VULKAN
    vkCmdPipelineBarrier(impl_->activeCmdBuffer,
        static_cast<VkPipelineStageFlags>(srcStage),
        static_cast<VkPipelineStageFlags>(dstStage),
        0, 0, nullptr, 0, nullptr, 0, nullptr);
#endif
}

void CommandList::transitionImage(
    Image& image,
    ImageLayout oldLayout,
    ImageLayout newLayout,
    uint32_t aspectMask) {
#ifdef TASROVY_API_VULKAN
    const auto toVkLayout = [](ImageLayout layout) {
        switch (layout) {
        case ImageLayout::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case ImageLayout::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ImageLayout::DepthAttachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::ShaderRead:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ImageLayout::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        return VK_IMAGE_LAYOUT_UNDEFINED;
    };

    const auto inferAspectMask = [&]() -> VkImageAspectFlags {
        if (aspectMask != 0) {
            return static_cast<VkImageAspectFlags>(aspectMask);
        }
        if (oldLayout == ImageLayout::DepthAttachment ||
            newLayout == ImageLayout::DepthAttachment) {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    };

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = toVkLayout(oldLayout);
    barrier.newLayout = toVkLayout(newLayout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = reinterpret_cast<VkImage>(image.getNativeImage());
    barrier.subresourceRange.aspectMask = inferAspectMask();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.getMipLevels();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    if (newLayout == ImageLayout::ColorAttachment) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (newLayout == ImageLayout::DepthAttachment) {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (newLayout == ImageLayout::ShaderRead) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(
        impl_->activeCmdBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
#endif
}

} // namespace Tasrovy::RHI
