#include "AssetManager.hpp"

#include "AssetLoader.hpp"
#include <Logger.hpp>
#include <algorithm>
#include <thread>

namespace Tasrovy::FS {

AssetManager::AssetManager(size_t workerCount)
    : executor_(std::max<size_t>(1, workerCount == 0 ? std::thread::hardware_concurrency() : workerCount)) {
    LOG_INFO("AssetManager started with {} Taskflow worker threads", executor_.num_workers());
}

AssetManager::~AssetManager() {
    stop();
}

bool AssetManager::requestModel(const std::string& path) {
    return enqueue({AssetKind::Model, path, false, 0});
}

bool AssetManager::requestImage(const std::string& path, bool flipY, int desiredChannels) {
    return enqueue({AssetKind::Image, path, flipY, desiredChannels});
}

std::shared_ptr<Model> AssetManager::getModel(const std::string& path) const {
    std::lock_guard lock(mutex_);
    const auto found = models_.find(path);
    return found == models_.end() ? nullptr : found->second;
}

std::shared_ptr<Image> AssetManager::getImage(const std::string& path) const {
    std::lock_guard lock(mutex_);
    const auto found = images_.find(path);
    return found == images_.end() ? nullptr : found->second;
}

std::optional<AssetEvent> AssetManager::pollEvent() {
    std::lock_guard lock(mutex_);
    if (events_.empty()) {
        return std::nullopt;
    }

    auto event = std::move(events_.front());
    events_.pop();
    return event;
}

bool AssetManager::isIdle() const {
    std::lock_guard lock(mutex_);
    return activeTasks_ == 0;
}

void AssetManager::waitIdle() {
    std::unique_lock lock(mutex_);
    idleCv_.wait(lock, [this]() {
        return activeTasks_ == 0;
    });
}

void AssetManager::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }

    executor_.wait_for_all();
    idleCv_.notify_all();
}

bool AssetManager::enqueue(Task task) {
    if (task.path.empty()) {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (stopping_) {
        return false;
    }

    if (task.kind == AssetKind::Model) {
        if (models_.contains(task.path) || loadingModels_.contains(task.path)) {
            return false;
        }
        loadingModels_.insert(task.path);
    } else {
        if (images_.contains(task.path) || loadingImages_.contains(task.path)) {
            return false;
        }
        loadingImages_.insert(task.path);
    }

    ++activeTasks_;
    executor_.silent_async([this, task = std::move(task)]() mutable {
        executeTask(std::move(task));
    });
    return true;
}

void AssetManager::executeTask(Task task) {
    AssetLoader loader;
    std::shared_ptr<Model> model;
    std::shared_ptr<Image> image;

    AssetEvent event;
    event.kind = task.kind;
    event.path = task.path;

    if (task.kind == AssetKind::Model) {
        model = loader.LoadModel(task.path);
        event.success = model != nullptr;
        if (!event.success) {
            event.error = "failed to load model";
        }
    } else {
        image = std::make_shared<Image>();
        event.success = image->LoadFromFile(task.path, task.flipY, task.desiredChannels);
        if (!event.success) {
            event.error = "failed to load image";
        }
    }

    {
        std::lock_guard lock(mutex_);
        if (task.kind == AssetKind::Model) {
            loadingModels_.erase(task.path);
            if (model) {
                models_[task.path] = std::move(model);
            }
        } else {
            loadingImages_.erase(task.path);
            if (event.success) {
                images_[task.path] = std::move(image);
            }
        }
        events_.push(std::move(event));
        --activeTasks_;
        if (activeTasks_ == 0) {
            idleCv_.notify_all();
        }
    }
}

} // namespace Tasrovy::FS
