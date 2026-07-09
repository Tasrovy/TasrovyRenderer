#pragma once

#include "Image.hpp"
#include "Model.hpp"
#include <taskflow/taskflow.hpp>
#include <cstddef>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Tasrovy::FS {

enum class AssetKind {
    Model,
    Image
};

struct AssetEvent {
    AssetKind kind = AssetKind::Model;
    std::string path;
    bool success = false;
    std::string error;
};

class AssetManager {
public:
    explicit AssetManager(size_t workerCount = 0);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    bool requestModel(const std::string& path);
    bool requestImage(const std::string& path, bool flipY = false, int desiredChannels = 4);

    std::shared_ptr<Model> getModel(const std::string& path) const;
    std::shared_ptr<Image> getImage(const std::string& path) const;
    std::optional<AssetEvent> pollEvent();

    bool isIdle() const;
    void waitIdle();
    void stop();

private:
    struct Task {
        AssetKind kind = AssetKind::Model;
        std::string path;
        bool flipY = false;
        int desiredChannels = 4;
    };

    bool enqueue(Task task);
    void executeTask(Task task);

    mutable std::mutex mutex_;
    std::condition_variable idleCv_;
    tf::Executor executor_;
    std::queue<AssetEvent> events_;
    std::unordered_map<std::string, std::shared_ptr<Model>> models_;
    std::unordered_map<std::string, std::shared_ptr<Image>> images_;
    std::unordered_set<std::string> loadingModels_;
    std::unordered_set<std::string> loadingImages_;
    size_t activeTasks_ = 0;
    bool stopping_ = false;
};

} // namespace Tasrovy::FS
