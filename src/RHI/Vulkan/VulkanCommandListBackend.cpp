#include "VulkanCommandListBackend.h"

#include "VulkanContext.h"
#include "VulkanConversions.h"
#include "VulkanRenderOverlayBackend.h"
#include "../Buffer.h"
#include "../Descriptor.h"
#include "../Image.h"
#include "../Pass.h"
#include "../Pipeline.h"
#include "../RHIBackendAccess.h"
#include "../RenderOverlay.h"

#include <stdexcept>
#include <vector>

namespace Tasrovy::RHI::Vulkan {
namespace {

VkAttachmentLoadOp toVkLoadOp(RHIAttachmentLoad load) {
    switch (load) {
    case RHIAttachmentLoad::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RHIAttachmentLoad::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RHIAttachmentLoad::Discard: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

VkAttachmentStoreOp toVkStoreOp(RHIAttachmentStore store) {
    return store == RHIAttachmentStore::Store
        ? VK_ATTACHMENT_STORE_OP_STORE
        : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

VkPipelineStageFlags toVkPipelineStages(PipelineStage stages) {
    VkPipelineStageFlags result = 0;
    const auto add = [&](PipelineStage stage, VkPipelineStageFlagBits value) {
        if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0)
            result |= value;
    };
    add(PipelineStage::TopOfPipe, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    add(PipelineStage::Host, VK_PIPELINE_STAGE_HOST_BIT);
    add(PipelineStage::DrawIndirect, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
    add(PipelineStage::VertexShader, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    add(PipelineStage::FragmentShader, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    add(PipelineStage::ComputeShader, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    add(PipelineStage::Transfer, VK_PIPELINE_STAGE_TRANSFER_BIT);
    add(PipelineStage::ColorAttachmentOutput,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if ((static_cast<uint32_t>(stages) &
         static_cast<uint32_t>(PipelineStage::DepthStencilTests)) != 0) {
        result |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    add(PipelineStage::BottomOfPipe, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    add(PipelineStage::AllCommands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    return result != 0 ? result : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
}

VkAccessFlags toVkAccessFlags(ResourceAccess accesses) {
    VkAccessFlags result = 0;
    const auto add = [&](ResourceAccess access, VkAccessFlagBits value) {
        if ((static_cast<uint32_t>(accesses) & static_cast<uint32_t>(access)) != 0)
            result |= value;
    };
    add(ResourceAccess::HostWrite, VK_ACCESS_HOST_WRITE_BIT);
    add(ResourceAccess::IndirectCommandRead, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    add(ResourceAccess::ShaderRead, VK_ACCESS_SHADER_READ_BIT);
    add(ResourceAccess::ShaderWrite, VK_ACCESS_SHADER_WRITE_BIT);
    add(ResourceAccess::TransferRead, VK_ACCESS_TRANSFER_READ_BIT);
    add(ResourceAccess::TransferWrite, VK_ACCESS_TRANSFER_WRITE_BIT);
    add(ResourceAccess::ColorAttachmentRead, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
    add(ResourceAccess::ColorAttachmentWrite, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    add(ResourceAccess::DepthStencilRead,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    add(ResourceAccess::DepthStencilWrite,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    add(ResourceAccess::MemoryRead, VK_ACCESS_MEMORY_READ_BIT);
    add(ResourceAccess::MemoryWrite, VK_ACCESS_MEMORY_WRITE_BIT);
    return result;
}

void transitionNativeImage(
    VkCommandBuffer commandBuffer, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
    VkAccessFlags dstAccess) {
    if (image == VK_NULL_HANDLE) return;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

struct LayoutState {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
};

LayoutState stateForLayout(ImageLayout layout) {
    switch (layout) {
    case ImageLayout::Undefined:
        return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    case ImageLayout::ColorAttachment:
        return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case ImageLayout::DepthAttachment:
        return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case ImageLayout::DepthReadOnly:
        return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
    case ImageLayout::ShaderRead:
        return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT};
    case ImageLayout::General:
        return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    case ImageLayout::Present:
        return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_MEMORY_READ_BIT};
    }
    return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
}

} // namespace

VulkanCommandListBackend::VulkanCommandListBackend(VulkanContext& context)
    : context_(context) {}

VulkanCommandListBackend::~VulkanCommandListBackend() {
    if (commandPool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(context_.getDevice(), commandPool_, nullptr);
}

void VulkanCommandListBackend::begin() {
    if (commandPool_ == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex =
            context_.getQueueFamilyIndices().graphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(
                context_.getDevice(), &poolInfo, nullptr, &commandPool_) !=
            VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan command pool");
        }
    }
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(
            context_.getDevice(), &allocate, &commandBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffer");
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin Vulkan command buffer");
    recording_ = true;
}

void VulkanCommandListBackend::end() {
    if (!recording_) return;
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS)
        throw std::runtime_error("Failed to end Vulkan command buffer");
    recording_ = false;
}

void VulkanCommandListBackend::attachNativeCommandBuffer(uint64_t handle) {
    commandBuffer_ = reinterpret_cast<VkCommandBuffer>(handle);
    recording_ = handle != 0;
}
uint64_t VulkanCommandListBackend::nativeCommandBuffer() const {
    return reinterpret_cast<uint64_t>(commandBuffer_);
}

void VulkanCommandListBackend::beginRenderPass(Pass& pass) {
    const auto& desc = pass.getDesc();
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(desc.colorAttachments.size());
    for (const auto& attachment : desc.colorAttachments) {
        if (!attachment.image) continue;
        VkRenderingAttachmentInfo value{};
        value.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        value.imageView = reinterpret_cast<VkImageView>(
            BackendAccess::imageView(*attachment.image));
        value.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        value.loadOp = toVkLoadOp(attachment.load);
        value.storeOp = toVkStoreOp(attachment.store);
        value.clearValue.color = {{attachment.clearColor.x,
            attachment.clearColor.y, attachment.clearColor.z,
            attachment.clearColor.w}};
        colors.push_back(value);
    }
    VkRenderingAttachmentInfo depth{};
    const bool hasDepth = desc.depthAttachment && desc.depthAttachment->image;
    if (hasDepth) {
        const auto& attachment = *desc.depthAttachment;
        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = reinterpret_cast<VkImageView>(
            BackendAccess::imageView(*attachment.image));
        depth.imageLayout = attachment.readOnly
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = toVkLoadOp(attachment.load);
        depth.storeOp = toVkStoreOp(attachment.store);
        depth.clearValue.depthStencil = {attachment.clearDepth, 0};
    }
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {desc.width, desc.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    rendering.pColorAttachments = colors.data();
    rendering.pDepthAttachment = hasDepth ? &depth : nullptr;
    vkCmdBeginRendering(commandBuffer_, &rendering);
}

void VulkanCommandListBackend::endRenderPass() {
    vkCmdEndRendering(commandBuffer_);
}

void VulkanCommandListBackend::beginSwapchainRenderPass(
    const SwapchainRenderTarget& target) {
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.resolveImage),
        target.resolveImageWasPresented ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                        : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
        target.resolveImageWasPresented ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        target.resolveImageWasPresented ? VK_ACCESS_MEMORY_READ_BIT : 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.colorImage),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.depthImage),
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = reinterpret_cast<VkImageView>(target.colorView);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0, 0, 0, 1}};
    color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    color.resolveImageView = reinterpret_cast<VkImageView>(target.resolveView);
    color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = reinterpret_cast<VkImageView>(target.depthView);
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {target.width, target.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = target.depthView != 0 ? &depth : nullptr;
    vkCmdBeginRendering(commandBuffer_, &rendering);
}

void VulkanCommandListBackend::endSwapchainRenderPass(
    const SwapchainRenderTarget& target) {
    vkCmdEndRendering(commandBuffer_);
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
}

void VulkanCommandListBackend::renderOverlay(
    RenderOverlay& overlay, const SwapchainRenderTarget& target,
    uint64_t frameToken) {
    if (overlay.getGraphicsAPI() != GraphicsAPI::Vulkan)
        throw std::invalid_argument("Overlay backend mismatch");
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = reinterpret_cast<VkImageView>(target.resolveView);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, {target.width, target.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commandBuffer_, &rendering);
    auto* backend = static_cast<VulkanRenderOverlayBackend*>(
        overlay.getBackendImplementation());
    if (backend) backend->recordDrawData(commandBuffer_, frameToken);
    vkCmdEndRendering(commandBuffer_);
    transitionNativeImage(commandBuffer_,
        reinterpret_cast<VkImage>(target.resolveImage),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT);
}

void VulkanCommandListBackend::bindPipeline(Pipeline& pipeline, bool compute) {
    boundPipelineLayout_ = reinterpret_cast<VkPipelineLayout>(
        BackendAccess::pipelineLayout(pipeline));
    boundPipelineBindPoint_ = compute
        ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindPipeline(commandBuffer_, boundPipelineBindPoint_,
        reinterpret_cast<VkPipeline>(BackendAccess::pipeline(pipeline)));
    if (!compute) vkCmdSetFrontFace(commandBuffer_, VK_FRONT_FACE_CLOCKWISE);
}
void VulkanCommandListBackend::bindVertexBuffer(Buffer& buffer) {
    const auto value = reinterpret_cast<VkBuffer>(BackendAccess::buffer(buffer));
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &value, &offset);
}
void VulkanCommandListBackend::bindIndexBuffer(Buffer& buffer) {
    vkCmdBindIndexBuffer(commandBuffer_,
        reinterpret_cast<VkBuffer>(BackendAccess::buffer(buffer)),
        0, VK_INDEX_TYPE_UINT32);
}
void VulkanCommandListBackend::bindDescriptorSet(
    uint32_t setIndex, const DescriptorSet& descriptorSet) {
    const auto set = reinterpret_cast<VkDescriptorSet>(
        BackendAccess::descriptorSet(descriptorSet));
    vkCmdBindDescriptorSets(commandBuffer_, boundPipelineBindPoint_,
        boundPipelineLayout_, setIndex, 1, &set, 0, nullptr);
}
void VulkanCommandListBackend::setViewport(
    float x, float y, float width, float height,
    float minDepth, float maxDepth) {
    const VkViewport viewport{x, y, width, height, minDepth, maxDepth};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
}
void VulkanCommandListBackend::setScissor(
    int32_t x, int32_t y, uint32_t width, uint32_t height) {
    const VkRect2D scissor{{x, y}, {width, height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
}
void VulkanCommandListBackend::setVirtualShadowPage(
    const CommandList::VirtualShadowPageDesc& page) {
    if (!recording_ || page.pageSize == 0) return;
    const uint32_t x = page.pageX * page.pageSize;
    const uint32_t y = page.pageY * page.pageSize;
    if (x + page.pageSize > page.atlasWidth ||
        y + page.pageSize > page.atlasHeight) return;
    VkViewport viewport{};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(y);
    viewport.width = static_cast<float>(page.pageSize);
    viewport.height = static_cast<float>(page.pageSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    const VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)},
        {page.pageSize, page.pageSize}};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
}
void VulkanCommandListBackend::setFrontFace(FrontFace frontFace) {
    vkCmdSetFrontFace(commandBuffer_, toVkFrontFace(frontFace));
}
void VulkanCommandListBackend::draw(
    uint32_t vertexCount, uint32_t instanceCount) {
    vkCmdDraw(commandBuffer_, vertexCount, instanceCount, 0, 0);
}
void VulkanCommandListBackend::drawIndexed(
    uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
    int32_t vertexOffset, uint32_t firstInstance) {
    vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount,
        firstIndex, vertexOffset, firstInstance);
}
void VulkanCommandListBackend::drawIndexedIndirect(
    Buffer& indirectBuffer, uint64_t offset,
    uint32_t drawCount, uint32_t stride) {
    vkCmdDrawIndexedIndirect(commandBuffer_,
        reinterpret_cast<VkBuffer>(BackendAccess::buffer(indirectBuffer)),
        offset, drawCount, stride);
}
void VulkanCommandListBackend::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(commandBuffer_, x, y, z);
}
void VulkanCommandListBackend::copyBuffer(
    Buffer& src, Buffer& dst, uint64_t size) {
    const VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(commandBuffer_,
        reinterpret_cast<VkBuffer>(BackendAccess::buffer(src)),
        reinterpret_cast<VkBuffer>(BackendAccess::buffer(dst)), 1, &copy);
}
void VulkanCommandListBackend::pipelineBarrier(
    PipelineStage srcStage, PipelineStage dstStage) {
    vkCmdPipelineBarrier(commandBuffer_, toVkPipelineStages(srcStage),
        toVkPipelineStages(dstStage), 0, 0, nullptr, 0, nullptr, 0, nullptr);
}
void VulkanCommandListBackend::bufferMemoryBarrier(
    Buffer& buffer, PipelineStage srcStage, PipelineStage dstStage,
    ResourceAccess srcAccess, ResourceAccess dstAccess) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = toVkAccessFlags(srcAccess);
    barrier.dstAccessMask = toVkAccessFlags(dstAccess);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = reinterpret_cast<VkBuffer>(BackendAccess::buffer(buffer));
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer_, toVkPipelineStages(srcStage),
        toVkPipelineStages(dstStage), 0, 0, nullptr, 1, &barrier, 0, nullptr);
}
void VulkanCommandListBackend::transitionImage(
    Image& image, ImageLayout oldLayout, ImageLayout newLayout,
    uint32_t aspectMask) {
    const bool depth = image.getFormat() == Format::Depth32Float ||
        image.getFormat() == Format::Depth32FloatStencil8;
    const bool stencil = image.getFormat() == Format::Depth32FloatStencil8;
    const auto aspect = aspectMask != 0
        ? static_cast<VkImageAspectFlags>(aspectMask)
        : depth ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT |
              (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u))
                : VK_IMAGE_ASPECT_COLOR_BIT;
    const auto src = stateForLayout(oldLayout);
    const auto dst = stateForLayout(newLayout);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = toVkImageLayout(oldLayout);
    barrier.newLayout = toVkImageLayout(newLayout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = reinterpret_cast<VkImage>(BackendAccess::image(image));
    barrier.subresourceRange = {aspect, 0, image.getMipLevels(), 0, 1};
    barrier.srcAccessMask = src.access;
    barrier.dstAccessMask = dst.access;
    vkCmdPipelineBarrier(commandBuffer_, src.stage, dst.stage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}
void VulkanCommandListBackend::resetTimestampQueryPool(
    uint64_t queryPool, uint32_t queryCount) {
    if (queryPool != 0 && queryCount != 0)
        vkCmdResetQueryPool(commandBuffer_,
            reinterpret_cast<VkQueryPool>(queryPool), 0, queryCount);
}
void VulkanCommandListBackend::writeTimestamp(
    uint64_t queryPool, uint32_t queryIndex, bool begin) {
    if (queryPool != 0)
        vkCmdWriteTimestamp(commandBuffer_,
            begin ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                  : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            reinterpret_cast<VkQueryPool>(queryPool), queryIndex);
}

} // namespace Tasrovy::RHI::Vulkan
