#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace Tasrovy::Renderer {

class RenderThread {
public:
    using EntryPoint = std::function<void()>;

    ~RenderThread();
    void start(EntryPoint entryPoint);
    void stop();
    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace Tasrovy::Renderer
