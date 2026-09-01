#include "VulkanFrameSchedulerBackend.h"

#include "ImmediateSubmitter.h"
#include "Renderer.h"
#include "VulkanContext.h"
#include "VulkanConversions.h"
#include "VulkanQueue.h"
#include "VulkanSwapChain.h"
#include "../RHIBackendAccess.h"

#include <mutex>
#include <stdexcept>

namespace Tasrovy::RHI::Vulkan {

VulkanFrameSchedulerBackend::VulkanFrameSchedulerBackend(
    VulkanContext& context,
    Renderer& renderer,
    VulkanSwapchain& swapchain,
    VulkanQueue& graphicsQueue,
    VulkanQueue& presentQueue,
    ImmediateSubmitter& immediateSubmitter)
    : context_(context),
      renderer_(renderer),
      swapchain_(swapchain),
      graphicsQueue_(graphicsQueue),
      presentQueue_(presentQueue),
      immediateSubmitter_(immediateSubmitter) {
}

bool VulkanFrameSchedulerBackend::beginFrame(CommandList& commandList) {
    const auto commandBuffer = renderer_.beginFrame(swapchain_);
    if (!commandBuffer) return false;
    BackendAccess::attachCommandBuffer(commandList,
        reinterpret_cast<uint64_t>(commandBuffer));
    activeCommandList_ = &commandList;
    return true;
}

void VulkanFrameSchedulerBackend::beginOverlay(CommandList& commandList) {
    if (activeCommandList_ != &commandList) {
        throw std::logic_error(
            "Overlay recording requires the active frame CommandList");
    }
    const auto commandBuffer = renderer_.beginOverlayCommands();
    BackendAccess::attachCommandBuffer(
        commandList, reinterpret_cast<uint64_t>(commandBuffer));
}

void VulkanFrameSchedulerBackend::submitFrame() {
    renderer_.endFrame(swapchain_, graphicsQueue_, presentQueue_);
    if (activeCommandList_) BackendAccess::attachCommandBuffer(*activeCommandList_, 0);
    activeCommandList_ = nullptr;
}

void VulkanFrameSchedulerBackend::abortFrame() {
    renderer_.abortFrame(graphicsQueue_);
    if (activeCommandList_) BackendAccess::attachCommandBuffer(*activeCommandList_, 0);
    activeCommandList_ = nullptr;
}

void VulkanFrameSchedulerBackend::waitForInFlightFrames() {
    renderer_.waitIdle();
}

void VulkanFrameSchedulerBackend::executeImmediate(
    CommandList& commandList,
    const std::function<void(CommandList&)>& recordCommands) {
    immediateSubmitter_.submit([&](VkCommandBuffer nativeCommandBuffer) {
        BackendAccess::attachCommandBuffer(commandList,
            reinterpret_cast<uint64_t>(nativeCommandBuffer));
        recordCommands(commandList);
        BackendAccess::attachCommandBuffer(commandList, 0);
    });
}

bool VulkanFrameSchedulerBackend::recreateSwapchain(
    uint32_t width,
    uint32_t height) {
    if (width == 0 || height == 0) return false;
    renderer_.waitIdle();
    {
        std::scoped_lock queueLock(context_.getQueueMutex());
        const VkQueue graphicsQueue = graphicsQueue_.getQueue();
        const VkQueue presentQueue = presentQueue_.getQueue();
        VkResult result = vkQueueWaitIdle(graphicsQueue);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to idle graphics queue for swapchain recreation");
        }
        if (presentQueue != graphicsQueue &&
            vkQueueWaitIdle(presentQueue) != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to idle present queue for swapchain recreation");
        }
    }
    context_.updateFramebufferSize(
        static_cast<int>(width), static_cast<int>(height));
    swapchain_.recreate();
    renderer_.onSwapchainRecreated(swapchain_.getImageCount());
    return true;
}

bool VulkanFrameSchedulerBackend::isSwapchainRebuildRequired() const {
    return renderer_.isSwapchainRebuildRequired();
}

uint32_t VulkanFrameSchedulerBackend::getCurrentFrameIndex() const {
    return renderer_.getCurrentFrame();
}
uint32_t VulkanFrameSchedulerBackend::getWidth() const {
    return swapchain_.getExtent().width;
}
uint32_t VulkanFrameSchedulerBackend::getHeight() const {
    return swapchain_.getExtent().height;
}
Format VulkanFrameSchedulerBackend::getColorFormat() const {
    return fromVkFormat(swapchain_.getImageFormat());
}
Format VulkanFrameSchedulerBackend::getDepthFormat() const {
    return fromVkFormat(context_.findDepthFormat());
}
uint32_t VulkanFrameSchedulerBackend::getMaxFramesInFlight() const {
    return renderer_.getMaxFramesInFlight();
}

SwapchainRenderTarget
VulkanFrameSchedulerBackend::getCurrentSwapchainTarget() const {
    SwapchainRenderTarget target{};
    const uint32_t imageIndex = renderer_.getImageIndex();
    target.colorImage = reinterpret_cast<uint64_t>(
        swapchain_.getColorAttachmentImage());
    target.colorView = reinterpret_cast<uint64_t>(
        swapchain_.getColorAttachmentView());
    target.resolveImage = reinterpret_cast<uint64_t>(
        swapchain_.getImage(imageIndex));
    target.resolveView = reinterpret_cast<uint64_t>(
        swapchain_.getImageView(imageIndex));
    target.depthImage = reinterpret_cast<uint64_t>(
        swapchain_.getDepthAttachmentImage());
    target.depthView = reinterpret_cast<uint64_t>(
        swapchain_.getDepthAttachmentView());
    target.width = swapchain_.getExtent().width;
    target.height = swapchain_.getExtent().height;
    target.resolveImageWasPresented =
        swapchain_.getImageLayout(imageIndex) ==
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    return target;
}

void VulkanFrameSchedulerBackend::markCurrentSwapchainImagePresented() {
    swapchain_.setImageLayout(
        renderer_.getImageIndex(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

std::vector<double>
VulkanFrameSchedulerBackend::consumeGpuTimestampDurations() {
    return renderer_.consumeGpuTimestampDurations();
}

uint64_t VulkanFrameSchedulerBackend::getCurrentTimestampQueryPool() const {
    return reinterpret_cast<uint64_t>(
        renderer_.getCurrentTimestampQueryPool());
}

void VulkanFrameSchedulerBackend::setCurrentTimestampQueryCount(
    uint32_t queryCount) {
    renderer_.setCurrentTimestampQueryCount(queryCount);
}

} // namespace Tasrovy::RHI::Vulkan
