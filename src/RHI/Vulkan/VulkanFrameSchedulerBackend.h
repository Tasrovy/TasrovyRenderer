#pragma once

#include "../FrameSchedulerBackend.h"

#include <memory>

class VulkanContext;
class Renderer;
class VulkanSwapchain;
class VulkanQueue;
class ImmediateSubmitter;

namespace Tasrovy::RHI::Vulkan {

class VulkanFrameSchedulerBackend final : public IFrameSchedulerBackend {
public:
    VulkanFrameSchedulerBackend(
        VulkanContext& context,
        Renderer& renderer,
        VulkanSwapchain& swapchain,
        VulkanQueue& graphicsQueue,
        VulkanQueue& presentQueue,
        ImmediateSubmitter& immediateSubmitter);

    bool beginFrame(CommandList& commandList) override;
    void beginOverlay(CommandList& commandList) override;
    void submitFrame() override;
    void abortFrame() override;
    void waitForInFlightFrames() override;
    void executeImmediate(
        CommandList& commandList,
        const std::function<void(CommandList&)>& recordCommands) override;
    bool recreateSwapchain(uint32_t width, uint32_t height) override;
    bool isSwapchainRebuildRequired() const override;
    uint32_t getCurrentFrameIndex() const override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;
    Format getColorFormat() const override;
    Format getDepthFormat() const override;
    uint32_t getMaxFramesInFlight() const override;
    SwapchainRenderTarget getCurrentSwapchainTarget() const override;
    void markCurrentSwapchainImagePresented() override;
    std::vector<double> consumeGpuTimestampDurations() override;
    uint64_t getCurrentTimestampQueryPool() const override;
    void setCurrentTimestampQueryCount(uint32_t queryCount) override;

private:
    VulkanContext& context_;
    Renderer& renderer_;
    VulkanSwapchain& swapchain_;
    VulkanQueue& graphicsQueue_;
    VulkanQueue& presentQueue_;
    ImmediateSubmitter& immediateSubmitter_;
    CommandList* activeCommandList_ = nullptr;
};

} // namespace Tasrovy::RHI::Vulkan
