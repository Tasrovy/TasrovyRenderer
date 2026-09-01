#pragma once

#include "CommandList.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Tasrovy::RHI {

class IFrameSchedulerBackend {
public:
    virtual ~IFrameSchedulerBackend() = default;
    virtual bool beginFrame(CommandList& commandList) = 0;
    virtual void beginOverlay(CommandList& commandList) = 0;
    virtual void submitFrame() = 0;
    virtual void abortFrame() = 0;
    virtual void waitForInFlightFrames() = 0;
    virtual void executeImmediate(
        CommandList& commandList,
        const std::function<void(CommandList&)>& recordCommands) = 0;
    virtual bool recreateSwapchain(uint32_t width, uint32_t height) = 0;
    virtual bool isSwapchainRebuildRequired() const = 0;
    virtual uint32_t getCurrentFrameIndex() const = 0;
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;
    virtual Format getColorFormat() const = 0;
    virtual Format getDepthFormat() const = 0;
    virtual uint32_t getMaxFramesInFlight() const = 0;
    virtual SwapchainRenderTarget getCurrentSwapchainTarget() const = 0;
    virtual void markCurrentSwapchainImagePresented() = 0;
    virtual std::vector<double> consumeGpuTimestampDurations() = 0;
    virtual uint64_t getCurrentTimestampQueryPool() const = 0;
    virtual void setCurrentTimestampQueryCount(uint32_t queryCount) = 0;
};

} // namespace Tasrovy::RHI
