#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace Tasrovy::Renderer {

// Single-consumer owner of frame command recording and RHI submission. The
// bounded queue prevents the render producer from outrunning frame resources.
class RHIThread {
public:
    explicit RHIThread(size_t maximumOutstandingTasks = 2);
    ~RHIThread();

    RHIThread(const RHIThread&) = delete;
    RHIThread& operator=(const RHIThread&) = delete;

    void start();
    void stop();
    bool running() const;
    bool isCurrentThread() const;

    std::future<void> submit(std::function<void()> task);
    void invoke(std::function<void()> task);
    void drain();

private:
    struct WorkItem {
        std::function<void()> task;
        std::promise<void> completion;
    };

    void workerLoop();

    const size_t maximumOutstandingTasks_;
    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable capacityAvailable_;
    std::condition_variable drained_;
    std::deque<WorkItem> queue_;
    std::thread thread_;
    std::thread::id workerThreadId_{};
    size_t outstandingTasks_ = 0;
    bool accepting_ = false;
    bool stopping_ = false;
};

} // namespace Tasrovy::Renderer
