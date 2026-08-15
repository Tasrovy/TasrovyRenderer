#include "RHIThread.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Tasrovy::Renderer {

RHIThread::RHIThread(size_t maximumOutstandingTasks)
    : maximumOutstandingTasks_(
          std::max<size_t>(maximumOutstandingTasks, 1u)) {}

RHIThread::~RHIThread() {
    stop();
}

void RHIThread::start() {
    std::scoped_lock lock(mutex_);
    if (accepting_ || thread_.joinable()) return;
    accepting_ = true;
    stopping_ = false;
    thread_ = std::thread([this] { workerLoop(); });
}

void RHIThread::stop() {
    {
        std::scoped_lock lock(mutex_);
        if (!thread_.joinable()) {
            accepting_ = false;
            stopping_ = true;
            return;
        }
        accepting_ = false;
        stopping_ = true;
    }
    workAvailable_.notify_all();
    capacityAvailable_.notify_all();
    thread_.join();
}

bool RHIThread::running() const {
    std::scoped_lock lock(mutex_);
    return accepting_ && !stopping_;
}

bool RHIThread::isCurrentThread() const {
    std::scoped_lock lock(mutex_);
    return workerThreadId_ == std::this_thread::get_id();
}

std::future<void> RHIThread::submit(std::function<void()> task) {
    if (!task) {
        throw std::invalid_argument("RHIThread cannot submit an empty task");
    }

    WorkItem item;
    item.task = std::move(task);
    auto completion = item.completion.get_future();
    {
        std::unique_lock lock(mutex_);
        capacityAvailable_.wait(lock, [this] {
            return !accepting_ ||
                outstandingTasks_ < maximumOutstandingTasks_;
        });
        if (!accepting_ || stopping_) {
            throw std::runtime_error("RHIThread is not accepting work");
        }
        ++outstandingTasks_;
        queue_.push_back(std::move(item));
    }
    workAvailable_.notify_one();
    return completion;
}

void RHIThread::invoke(std::function<void()> task) {
    submit(std::move(task)).get();
}

void RHIThread::drain() {
    std::unique_lock lock(mutex_);
    drained_.wait(lock, [this] { return outstandingTasks_ == 0; });
}

void RHIThread::workerLoop() {
    {
        std::scoped_lock lock(mutex_);
        workerThreadId_ = std::this_thread::get_id();
    }

    for (;;) {
        WorkItem item;
        {
            std::unique_lock lock(mutex_);
            workAvailable_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });
            if (queue_.empty()) {
                if (stopping_) break;
                continue;
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }

        try {
            item.task();
            item.completion.set_value();
        } catch (...) {
            item.completion.set_exception(std::current_exception());
        }

        {
            std::scoped_lock lock(mutex_);
            --outstandingTasks_;
            if (outstandingTasks_ == 0) drained_.notify_all();
        }
        capacityAvailable_.notify_all();
    }

    std::scoped_lock lock(mutex_);
    workerThreadId_ = {};
    accepting_ = false;
    if (outstandingTasks_ == 0) drained_.notify_all();
    capacityAvailable_.notify_all();
}

} // namespace Tasrovy::Renderer
