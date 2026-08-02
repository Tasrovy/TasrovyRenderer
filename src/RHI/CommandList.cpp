#include "CommandList.h"
#include "Buffer.h"
#include "Image.h"
#include "Pass.h"
#include "Descriptor.h"
#include "RHIConfig.h"
#include "RenderOverlay.h"
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
    VkPipelineBindPoint boundPipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
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
void transitionNativeImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspect,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    VkAccessFlags srcAccess,
    VkAccessFlags dstAccess) {
    if (image == VK_NULL_HANDLE) {
        return;
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(
        commandBuffer, srcStage, dstStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

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

VkPipelineStageFlags toVkPipelineStages(PipelineStage stages) {
    const auto bits = static_cast<uint32_t>(stages);
    VkPipelineStageFlags result = 0;
    const auto add = [&](PipelineStage stage, VkPipelineStageFlagBits vkStage) {
        if ((bits & static_cast<uint32_t>(stage)) != 0) {
            result |= vkStage;
        }
    };
    add(PipelineStage::TopOfPipe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    add(PipelineStage::Host, VK_PIPELINE_STAGE_HOST_BIT);
    add(PipelineStage::DrawIndirect, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
    add(PipelineStage::VertexShader, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    add(PipelineStage::FragmentShader, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    add(PipelineStage::ComputeShader, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    add(PipelineStage::Transfer, VK_PIPELINE_STAGE_TRANSFER_BIT);
    add(
        PipelineStage::ColorAttachmentOutput,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if ((bits & static_cast<uint32_t>(PipelineStage::DepthStencilTests)) != 0) {
        result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    add(PipelineStage::BottomOfPipe, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    add(PipelineStage::AllCommands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    return result != 0 ? result : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
}

VkAccessFlags toVkAccessFlags(ResourceAccess accesses) {
    const auto bits = static_cast<uint32_t>(accesses);
    VkAccessFlags result = 0;
    const auto add = [&](ResourceAccess access, VkAccessFlagBits vkAccess) {
        if ((bits & static_cast<uint32_t>(access)) != 0) {
            result |= vkAccess;
        }
    };
    add(ResourceAccess::HostWrite, VK_ACCESS_HOST_WRITE_BIT);
    add(
        ResourceAccess::IndirectCommandRead,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    add(ResourceAccess::ShaderRead, VK_ACCESS_SHADER_READ_BIT);
    add(ResourceAccess::ShaderWrite, VK_ACCESS_SHADER_WRITE_BIT);
    add(ResourceAccess::TransferRead, VK_ACCESS_TRANSFER_READ_BIT);
    add(ResourceAccess::TransferWrite, VK_ACCESS_TRANSFER_WRITE_BIT);
    add(
        ResourceAccess::ColorAttachmentRead,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
    add(
        ResourceAccess::ColorAttachmentWrite,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    add(
        ResourceAccess::DepthStencilRead,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    add(
        ResourceAccess::DepthStencilWrite,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    add(ResourceAccess::MemoryRead, VK_ACCESS_MEMORY_READ_BIT);
    add(ResourceAccess::MemoryWrite, VK_ACCESS_MEMORY_WRITE_BIT);
    return result;
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

    // Resolve attachment (for MSAA) 鈥?set on color attachment, not VkRenderingInfo
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

void CommandList::beginSwapchainRenderPass(const SwapchainRenderTarget& target) {
#ifdef TASROVY_API_VULKAN
    const auto cmd = impl_->activeCmdBuffer;
    transitionNativeImage(
        cmd,
        reinterpret_cast<VkImage>(target.resolveImage),
        target.resolveImageWasPresented
            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        target.resolveImageWasPresented
            ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        target.resolveImageWasPresented ? VK_ACCESS_MEMORY_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    transitionNativeImage(
        cmd,
        reinterpret_cast<VkImage>(target.colorImage),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    transitionNativeImage(
        cmd,
        reinterpret_cast<VkImage>(target.depthImage),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        // VulkanContext currently selects D32_SFLOAT_S8_UINT for the
        // swapchain depth attachment, so the layout transition must cover
        // both aspects when separate depth/stencil layouts are unavailable.
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = reinterpret_cast<VkImageView>(target.colorView);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = reinterpret_cast<VkImageView>(target.resolveView);
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = reinterpret_cast<VkImageView>(target.depthView);
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, {target.width, target.height}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = target.depthView != 0 ? &depthAttachment : nullptr;
    vkCmdBeginRendering(cmd, &renderingInfo);
#else
    (void)target;
#endif
}

void CommandList::endSwapchainRenderPass(const SwapchainRenderTarget& target) {
#ifdef TASROVY_API_VULKAN
    vkCmdEndRendering(impl_->activeCmdBuffer);
    transitionNativeImage(
        impl_->activeCmdBuffer,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT);
#else
    (void)target;
#endif
}

void CommandList::renderOverlay(
    RenderOverlay& overlay,
    const SwapchainRenderTarget& target) {
#ifdef TASROVY_API_VULKAN
    transitionNativeImage(
        impl_->activeCmdBuffer,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = reinterpret_cast<VkImageView>(target.resolveView);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, {target.width, target.height}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(impl_->activeCmdBuffer, &renderingInfo);
    overlay.recordDrawData(
        reinterpret_cast<uint64_t>(impl_->activeCmdBuffer));
    vkCmdEndRendering(impl_->activeCmdBuffer);
    transitionNativeImage(
        impl_->activeCmdBuffer,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT);
#else
    (void)overlay;
    (void)target;
#endif
}

// --- Pipeline ---

void CommandList::bindPipeline(uint64_t nativePipeline, uint64_t nativeLayout, uint32_t bindPoint) {
#ifdef TASROVY_API_VULKAN
    impl_->boundPipelineLayout = reinterpret_cast<VkPipelineLayout>(nativeLayout);
    impl_->boundPipelineBindPoint =
        bindPoint == 0
            ? VK_PIPELINE_BIND_POINT_GRAPHICS
            : VK_PIPELINE_BIND_POINT_COMPUTE;
    vkCmdBindPipeline(impl_->activeCmdBuffer,
        impl_->boundPipelineBindPoint,
        reinterpret_cast<VkPipeline>(nativePipeline));
    if (bindPoint == 0) {
        vkCmdSetFrontFace(impl_->activeCmdBuffer, VK_FRONT_FACE_CLOCKWISE);
    }
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
    vkCmdBindDescriptorSets(impl_->activeCmdBuffer, impl_->boundPipelineBindPoint,
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

void CommandList::setVirtualShadowPage(const VirtualShadowPageDesc& page) {
#ifdef TASROVY_API_VULKAN
    if (!impl_->isRecording || page.pageSize == 0) {
        return;
    }
    const uint32_t x = page.pageX * page.pageSize;
    const uint32_t y = page.pageY * page.pageSize;
    if (x + page.pageSize > page.atlasWidth ||
        y + page.pageSize > page.atlasHeight) {
        return;
    }

    VkViewport viewport{};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(y);
    viewport.width = static_cast<float>(page.pageSize);
    viewport.height = static_cast<float>(page.pageSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(impl_->activeCmdBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {
        static_cast<int32_t>(x),
        static_cast<int32_t>(y)
    };
    scissor.extent = {page.pageSize, page.pageSize};
    vkCmdSetScissor(impl_->activeCmdBuffer, 0, 1, &scissor);
#else
    (void)page;
#endif
}

void CommandList::setFrontFace(uint32_t frontFace) {
#ifdef TASROVY_API_VULKAN
    vkCmdSetFrontFace(
        impl_->activeCmdBuffer,
        static_cast<VkFrontFace>(frontFace));
#endif
}

// --- Draw ---

void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount) {
#ifdef TASROVY_API_VULKAN
    vkCmdDraw(impl_->activeCmdBuffer, vertexCount, instanceCount, 0, 0);
#endif
}

void CommandList::drawIndexed(
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    int32_t vertexOffset,
    uint32_t firstInstance) {
#ifdef TASROVY_API_VULKAN
    vkCmdDrawIndexed(
        impl_->activeCmdBuffer,
        indexCount,
        instanceCount,
        firstIndex,
        vertexOffset,
        firstInstance);
#endif
}

void CommandList::drawIndexedIndirect(
    Buffer& indirectBuffer,
    uint64_t offset,
    uint32_t drawCount,
    uint32_t stride) {
#ifdef TASROVY_API_VULKAN
    vkCmdDrawIndexedIndirect(
        impl_->activeCmdBuffer,
        reinterpret_cast<VkBuffer>(indirectBuffer.getNativeHandle()),
        static_cast<VkDeviceSize>(offset),
        drawCount,
        stride);
#endif
}

void CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
#ifdef TASROVY_API_VULKAN
    vkCmdDispatch(impl_->activeCmdBuffer, x, y, z);
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

void CommandList::pipelineBarrier(
    PipelineStage srcStage,
    PipelineStage dstStage) {
#ifdef TASROVY_API_VULKAN
    vkCmdPipelineBarrier(impl_->activeCmdBuffer,
        toVkPipelineStages(srcStage),
        toVkPipelineStages(dstStage),
        0, 0, nullptr, 0, nullptr, 0, nullptr);
#endif
}

void CommandList::bufferMemoryBarrier(
    Buffer& buffer,
    PipelineStage srcStage,
    PipelineStage dstStage,
    ResourceAccess srcAccess,
    ResourceAccess dstAccess) {
#ifdef TASROVY_API_VULKAN
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = toVkAccessFlags(srcAccess);
    barrier.dstAccessMask = toVkAccessFlags(dstAccess);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = reinterpret_cast<VkBuffer>(buffer.getNativeHandle());
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(
        impl_->activeCmdBuffer,
        toVkPipelineStages(srcStage),
        toVkPipelineStages(dstStage),
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr);
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
        case ImageLayout::DepthReadOnly:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::ShaderRead:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ImageLayout::General:
            return VK_IMAGE_LAYOUT_GENERAL;
        case ImageLayout::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        return VK_IMAGE_LAYOUT_UNDEFINED;
    };

    const auto inferAspectMask = [&]() -> VkImageAspectFlags {
        if (aspectMask != 0) {
            return static_cast<VkImageAspectFlags>(aspectMask);
        }
        const auto format = static_cast<VkFormat>(image.getFormat());
        const bool isDepth =
            format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT;
        if (isDepth ||
            oldLayout == ImageLayout::DepthAttachment ||
            oldLayout == ImageLayout::DepthReadOnly ||
            newLayout == ImageLayout::DepthAttachment ||
            newLayout == ImageLayout::DepthReadOnly) {
            const bool hasStencil =
                format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                format == VK_FORMAT_D24_UNORM_S8_UINT;
            return VK_IMAGE_ASPECT_DEPTH_BIT |
                (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u);
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

    struct BarrierState {
        VkPipelineStageFlags stage;
        VkAccessFlags access;
    };
    const auto stateForLayout = [](ImageLayout layout, bool destination) -> BarrierState {
        switch (layout) {
        case ImageLayout::Undefined:
            return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
        case ImageLayout::ColorAttachment:
            return {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        case ImageLayout::DepthAttachment:
            return {
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case ImageLayout::DepthReadOnly:
            return {
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
        case ImageLayout::ShaderRead:
            return {
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT};
        case ImageLayout::General:
            return {
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        case ImageLayout::Present:
            return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT};
        }
        return {
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            static_cast<VkAccessFlags>(
                destination ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT
                            : VK_ACCESS_MEMORY_WRITE_BIT)};
    };

    const auto src = stateForLayout(oldLayout, false);
    const auto dst = stateForLayout(newLayout, true);
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    const VkPipelineStageFlags srcStage = src.stage;
    const VkPipelineStageFlags dstStage = dst.stage;

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

void CommandList::resetTimestampQueryPool(
    uint64_t nativeQueryPool,
    uint32_t queryCount) {
#ifdef TASROVY_API_VULKAN
    if (nativeQueryPool != 0 && queryCount > 0) {
        vkCmdResetQueryPool(
            impl_->activeCmdBuffer,
            reinterpret_cast<VkQueryPool>(nativeQueryPool),
            0,
            queryCount);
    }
#else
    (void)nativeQueryPool;
    (void)queryCount;
#endif
}

void CommandList::writeTimestamp(
    uint64_t nativeQueryPool,
    uint32_t queryIndex,
    bool begin) {
#ifdef TASROVY_API_VULKAN
    if (nativeQueryPool != 0) {
        vkCmdWriteTimestamp(
            impl_->activeCmdBuffer,
            begin ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            reinterpret_cast<VkQueryPool>(nativeQueryPool),
            queryIndex);
    }
#else
    (void)nativeQueryPool;
    (void)queryIndex;
    (void)begin;
#endif
}

} // namespace Tasrovy::RHI
