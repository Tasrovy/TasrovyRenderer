#include "FrameScheduler.h"

#include "FrameSchedulerBackend.h"

#include <utility>

namespace Tasrovy::RHI {

FrameScheduler::FrameScheduler(
    std::unique_ptr<IFrameSchedulerBackend> backend)
    : backend_(std::move(backend)) {
}

FrameScheduler::~FrameScheduler() = default;

bool FrameScheduler::beginFrame(CommandList& commandList) {
    return backend_->beginFrame(commandList);
}

void FrameScheduler::beginOverlay(CommandList& commandList) {
    backend_->beginOverlay(commandList);
}

void FrameScheduler::submitFrame() { backend_->submitFrame(); }
void FrameScheduler::abortFrame() { backend_->abortFrame(); }
void FrameScheduler::waitForInFlightFrames() { backend_->waitForInFlightFrames(); }

void FrameScheduler::executeImmediate(
    CommandList& commandList,
    const std::function<void(CommandList&)>& recordCommands) {
    backend_->executeImmediate(commandList, recordCommands);
}

bool FrameScheduler::recreateSwapchain(uint32_t width, uint32_t height) {
    return backend_->recreateSwapchain(width, height);
}

bool FrameScheduler::isSwapchainRebuildRequired() const {
    return backend_->isSwapchainRebuildRequired();
}

uint32_t FrameScheduler::getCurrentFrameIndex() const {
    return backend_->getCurrentFrameIndex();
}
uint32_t FrameScheduler::getWidth() const { return backend_->getWidth(); }
uint32_t FrameScheduler::getHeight() const { return backend_->getHeight(); }
Format FrameScheduler::getColorFormat() const { return backend_->getColorFormat(); }
Format FrameScheduler::getDepthFormat() const { return backend_->getDepthFormat(); }
uint32_t FrameScheduler::getMaxFramesInFlight() const {
    return backend_->getMaxFramesInFlight();
}

SwapchainRenderTarget FrameScheduler::getCurrentSwapchainTarget() const {
    return backend_->getCurrentSwapchainTarget();
}

void FrameScheduler::markCurrentSwapchainImagePresented() {
    backend_->markCurrentSwapchainImagePresented();
}

std::vector<double> FrameScheduler::consumeGpuTimestampDurations() {
    return backend_->consumeGpuTimestampDurations();
}

uint64_t FrameScheduler::getCurrentTimestampQueryPool() const {
    return backend_->getCurrentTimestampQueryPool();
}

void FrameScheduler::setCurrentTimestampQueryCount(uint32_t queryCount) {
    backend_->setCurrentTimestampQueryCount(queryCount);
}

} // namespace Tasrovy::RHI
