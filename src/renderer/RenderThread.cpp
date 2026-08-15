#include "RenderThread.h"

#include <utility>

namespace Tasrovy::Renderer {

RenderThread::~RenderThread() {
    stop();
}

void RenderThread::start(EntryPoint entryPoint) {
    if (thread_.joinable()) {
        return;
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return;
    }
    thread_ = std::thread(
        [this, entryPoint = std::move(entryPoint)]() mutable {
            entryPoint();
            running_.store(false, std::memory_order_release);
        });
}

void RenderThread::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

} // namespace Tasrovy::Renderer
