#include "FrameScheduler.h"
#include "RHIConfig.h"
#include <mutex>
#include <stdexcept>

#ifdef TASROVY_API_VULKAN
    #include "Vulkan/Renderer.h"
    #include "Vulkan/VulkanContext.h"
    #include "Vulkan/ImmediateSubmitter.h"
    #include "Vulkan/VulkanQueue.h"
    #include "Vulkan/VulkanSwapChain.h"
#endif

namespace Tasrovy::RHI {

struct FrameScheduler::Impl {
#ifdef TASROVY_API_VULKAN
    VulkanContext* context = nullptr;
    Renderer* renderer = nullptr;
    VulkanSwapchain* swapchain = nullptr;
    VulkanQueue* graphicsQueue = nullptr;
    VulkanQueue* presentQueue = nullptr;
    ImmediateSubmitter* immediateSubmitter = nullptr;
#endif
};

std::unique_ptr<FrameScheduler> FrameScheduler::create(
    void* nativeContext,
    void* nativeRenderer,
    void* nativeSwapchain,
    void* nativeGraphicsQueue,
    void* nativePresentQueue,
    void* nativeImmediateSubmitter) {
    auto scheduler = std::unique_ptr<FrameScheduler>(new FrameScheduler());
    scheduler->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    scheduler->impl_->context = static_cast<VulkanContext*>(nativeContext);
    scheduler->impl_->renderer = static_cast<Renderer*>(nativeRenderer);
    scheduler->impl_->swapchain = static_cast<VulkanSwapchain*>(nativeSwapchain);
    scheduler->impl_->graphicsQueue = static_cast<VulkanQueue*>(nativeGraphicsQueue);
    scheduler->impl_->presentQueue = static_cast<VulkanQueue*>(nativePresentQueue);
    scheduler->impl_->immediateSubmitter =
        static_cast<ImmediateSubmitter*>(nativeImmediateSubmitter);
#endif
    return scheduler;
}

FrameScheduler::~FrameScheduler() = default;

bool FrameScheduler::beginFrame(CommandList& commandList) {
#ifdef TASROVY_API_VULKAN
    const auto commandBuffer = impl_->renderer->beginFrame(*impl_->swapchain);
    if (!commandBuffer) {
        return false;
    }
    commandList.useNativeCommandBuffer(reinterpret_cast<uint64_t>(commandBuffer));
    return true;
#else
    (void)commandList;
    return false;
#endif
}

void FrameScheduler::submitFrame() {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->endFrame(
        *impl_->swapchain, *impl_->graphicsQueue, *impl_->presentQueue);
#endif
}

void FrameScheduler::waitForInFlightFrames() {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->waitIdle();
#endif
}

void FrameScheduler::executeImmediate(
    CommandList& commandList,
    const std::function<void(CommandList&)>& recordCommands) {
#ifdef TASROVY_API_VULKAN
    impl_->immediateSubmitter->submit([&](VkCommandBuffer nativeCommandBuffer) {
        commandList.useNativeCommandBuffer(
            reinterpret_cast<uint64_t>(nativeCommandBuffer));
        recordCommands(commandList);
        commandList.useNativeCommandBuffer(0);
    });
#else
    (void)commandList;
    (void)recordCommands;
#endif
}

bool FrameScheduler::recreateSwapchain(uint32_t width, uint32_t height) {
#ifdef TASROVY_API_VULKAN
    if (!impl_->context || !impl_->renderer || !impl_->swapchain ||
        !impl_->graphicsQueue || !impl_->presentQueue || width == 0 || height == 0) {
        return false;
    }

    // Graphics work is tracked by frame fences. Presentation has no matching
    // frame fence, so only the queues that access the swapchain are idled.
    impl_->renderer->waitIdle();
    {
        std::scoped_lock queueLock(impl_->context->getQueueMutex());
        const VkQueue graphicsQueue = impl_->graphicsQueue->getQueue();
        const VkQueue presentQueue = impl_->presentQueue->getQueue();
        VkResult result = vkQueueWaitIdle(graphicsQueue);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to idle graphics queue for swapchain recreation");
        }
        if (presentQueue != graphicsQueue) {
            result = vkQueueWaitIdle(presentQueue);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("failed to idle present queue for swapchain recreation");
            }
        }
    }

    impl_->context->updateFramebufferSize(
        static_cast<int>(width), static_cast<int>(height));
    impl_->swapchain->recreate();
    impl_->renderer->onSwapchainRecreated(impl_->swapchain->getImageCount());
    return true;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

bool FrameScheduler::isSwapchainRebuildRequired() const {
#ifdef TASROVY_API_VULKAN
    return impl_->renderer->isSwapchainRebuildRequired();
#else
    return false;
#endif
}

uint32_t FrameScheduler::getCurrentFrameIndex() const {
#ifdef TASROVY_API_VULKAN
    return impl_->renderer->getCurrentFrame();
#else
    return 0;
#endif
}

uint32_t FrameScheduler::getWidth() const {
#ifdef TASROVY_API_VULKAN
    return impl_->swapchain->getExtent().width;
#else
    return 0;
#endif
}

uint32_t FrameScheduler::getHeight() const {
#ifdef TASROVY_API_VULKAN
    return impl_->swapchain->getExtent().height;
#else
    return 0;
#endif
}

uint32_t FrameScheduler::getColorFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->swapchain->getImageFormat());
#else
    return 0;
#endif
}

uint32_t FrameScheduler::getDepthFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->context->findDepthFormat());
#else
    return 0;
#endif
}

uint32_t FrameScheduler::getMaxFramesInFlight() const {
#ifdef TASROVY_API_VULKAN
    return impl_->renderer->getMaxFramesInFlight();
#else
    return 0;
#endif
}

SwapchainRenderTarget FrameScheduler::getCurrentSwapchainTarget() const {
    SwapchainRenderTarget target{};
#ifdef TASROVY_API_VULKAN
    const uint32_t imageIndex = impl_->renderer->getImageIndex();
    target.colorImage = reinterpret_cast<uint64_t>(impl_->swapchain->getColorAttachmentImage());
    target.colorView = reinterpret_cast<uint64_t>(impl_->swapchain->getColorAttachmentView());
    target.resolveImage = reinterpret_cast<uint64_t>(impl_->swapchain->getImage(imageIndex));
    target.resolveView = reinterpret_cast<uint64_t>(impl_->swapchain->getImageView(imageIndex));
    target.depthImage = reinterpret_cast<uint64_t>(impl_->swapchain->getDepthAttachmentImage());
    target.depthView = reinterpret_cast<uint64_t>(impl_->swapchain->getDepthAttachmentView());
    target.width = impl_->swapchain->getExtent().width;
    target.height = impl_->swapchain->getExtent().height;
    target.resolveImageWasPresented =
        impl_->swapchain->getImageLayout(imageIndex) == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
#endif
    return target;
}

void FrameScheduler::markCurrentSwapchainImagePresented() {
#ifdef TASROVY_API_VULKAN
    impl_->swapchain->setImageLayout(
        impl_->renderer->getImageIndex(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
#endif
}

std::vector<double> FrameScheduler::consumeGpuTimestampDurations() {
#ifdef TASROVY_API_VULKAN
    return impl_->renderer->consumeGpuTimestampDurations();
#else
    return {};
#endif
}

uint64_t FrameScheduler::getCurrentTimestampQueryPool() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->renderer->getCurrentTimestampQueryPool());
#else
    return 0;
#endif
}

void FrameScheduler::setCurrentTimestampQueryCount(uint32_t queryCount) {
#ifdef TASROVY_API_VULKAN
    impl_->renderer->setCurrentTimestampQueryCount(queryCount);
#else
    (void)queryCount;
#endif
}

} // namespace Tasrovy::RHI
