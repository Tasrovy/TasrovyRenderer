#pragma once

#include "CommandList.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Tasrovy::RHI {

class IFrameSchedulerBackend;
class Device;

// Owns frame pacing policy: acquire, fence waits, queue submission, present,
// swapchain recreation and per-frame timestamp query ownership. GPU commands
// themselves are always recorded through CommandList.
class FrameScheduler {
public:
    ~FrameScheduler();

    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    bool beginFrame(CommandList& commandList);
    // Ends scene recording and attaches the per-frame UI command buffer to the
    // supplied CommandList. submitFrame() submits both buffers as one batch.
    void beginOverlay(CommandList& commandList);
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
    Format getColorFormat() const;
    Format getDepthFormat() const;
    uint32_t getMaxFramesInFlight() const;

    SwapchainRenderTarget getCurrentSwapchainTarget() const;
    void markCurrentSwapchainImagePresented();

    std::vector<double> consumeGpuTimestampDurations();
    uint64_t getCurrentTimestampQueryPool() const;
    void setCurrentTimestampQueryCount(uint32_t queryCount);

private:
    friend class Device;
    explicit FrameScheduler(std::unique_ptr<IFrameSchedulerBackend> backend);
    std::unique_ptr<IFrameSchedulerBackend> backend_;
};

} // namespace Tasrovy::RHI
