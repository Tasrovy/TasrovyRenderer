#include "ReflectionBridge.h"
#include "SpirvReflection.h"
#include "../render/Material.h"
#include <Logger.hpp>

namespace Tasrovy {

ReflectionBridge::~ReflectionBridge() {
    stop();
}

void ReflectionBridge::start() {
    if (running_.load()) return;
    running_.store(true, std::memory_order_release);
    workerThread_ = std::thread(&ReflectionBridge::workerLoop, this);
    LOG_INFO("ReflectionBridge worker thread started");
}

void ReflectionBridge::stop() {
    if (!running_.load()) return;
    running_.store(false, std::memory_order_release);
    requestCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    LOG_INFO("ReflectionBridge worker thread stopped");
}

bool ReflectionBridge::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

void ReflectionBridge::submitRequest(ReflectionRequest request) {
    {
        std::lock_guard lock(requestMutex_);
        requestQueue_.push(std::move(request));
    }
    requestCv_.notify_one();
}

size_t ReflectionBridge::pollResults() {
    size_t count = 0;
    std::lock_guard lock(resultMutex_);
    while (!resultQueue_.empty()) {
        auto result = std::move(resultQueue_.front());
        resultQueue_.pop();
        if (result.material && result.success) {
            result.material->applyReflection(result.vertexData, result.fragmentData);
        } else if (!result.success) {
            LOG_ERROR("SPIR-V reflection failed: {}", result.errorMessage);
        }
        ++count;
    }
    return count;
}

void ReflectionBridge::workerLoop() {
    while (running_.load(std::memory_order_acquire)) {
        ReflectionRequest request;
        {
            std::unique_lock lock(requestMutex_);
            requestCv_.wait(lock, [&] {
                return !requestQueue_.empty() || !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire) && requestQueue_.empty()) break;
            if (requestQueue_.empty()) continue;
            request = std::move(requestQueue_.front());
            requestQueue_.pop();
        }

        auto result = processRequest(request);

        {
            std::lock_guard lock(resultMutex_);
            resultQueue_.push(std::move(result));
        }
    }
}

ReflectionResult ReflectionBridge::processRequest(const ReflectionRequest& request) {
    ReflectionResult result;
    result.material = request.material;

    try {
        result.vertexData = SpirvReflection::reflect(request.vertSpvPath, request.vertEntryPoint);
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Vertex reflection failed: " + std::string(e.what());
        return result;
    }

    try {
        result.fragmentData = SpirvReflection::reflect(request.fragSpvPath, request.fragEntryPoint);
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = "Fragment reflection failed: " + std::string(e.what());
        return result;
    }

    return result;
}

} // namespace Tasrovy
