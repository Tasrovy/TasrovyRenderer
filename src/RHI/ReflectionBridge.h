#pragma once

#include "ReflectionData.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace Tasrovy {

class ReflectionBridge {
public:
    ReflectionBridge() = default;
    ~ReflectionBridge();

    ReflectionBridge(const ReflectionBridge&) = delete;
    ReflectionBridge& operator=(const ReflectionBridge&) = delete;

    void start();
    void stop();
    bool isRunning() const;

    void submitRequest(ReflectionRequest request);
    size_t pollResults();

private:
    void workerLoop();
    ReflectionResult processRequest(const ReflectionRequest& request);

    std::queue<ReflectionRequest> requestQueue_;
    std::mutex requestMutex_;
    std::condition_variable requestCv_;

    std::queue<ReflectionResult> resultQueue_;
    std::mutex resultMutex_;

    std::thread workerThread_;
    std::atomic<bool> running_{false};
};

} // namespace Tasrovy
