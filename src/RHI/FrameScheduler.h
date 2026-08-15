#pragma once

#include "CommandList.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Tasrovy::RHI {

// Owns frame pacing policy: acquire, fence waits, queue submission, present,
// swapchain recreation and per-frame timestamp query ownership. GPU commands
// themselves are always recorded through CommandList.
class FrameScheduler {
public:
    static std::unique_ptr<FrameScheduler> create(
        void* nativeContext,
        void* nativeRenderer,
        void* nativeSwapchain,
        void* nativeGraphicsQueue,
        void* nativePresentQueue,
        void* nativeImmediateSubmitter);
    ~FrameScheduler();

    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    bool beginFrame(CommandList& commandList);
    void submitFrame();
    void abortFrame();
    void waitForInFlightFrames();
    void executeImmediate(
        CommandList& commandList,
        const std::function<void(CommandList&)>& recordCommands);

    bool recreateSwapchain(uint32_t width, uint32_t height);
    bool isSwapchainRebuildRequired() const;

    uint32_t getCurrentFrameIndex() const;
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    uint32_t getColorFormat() const;
    uint32_t getDepthFormat() const;
    uint32_t getMaxFramesInFlight() const;

    SwapchainRenderTarget getCurrentSwapchainTarget() const;
    void markCurrentSwapchainImagePresented();

    std::vector<double> consumeGpuTimestampDurations();
    uint64_t getCurrentTimestampQueryPool() const;
    void setCurrentTimestampQueryCount(uint32_t queryCount);

private:
    FrameScheduler() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
