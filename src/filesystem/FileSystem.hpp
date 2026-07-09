#pragma once
#include <unordered_map>
#include <memory>
#include <string>
#include "Model.hpp"
#include "Image.hpp"
#include "Anim.hpp"
#include "AssetLoader.hpp"

namespace Tasrovy::FS {

class FileSystem {
public:
    FileSystem() = default;
    ~FileSystem() = default;

    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    std::shared_ptr<Model> LoadModel(const std::string& path);
    std::shared_ptr<Image> LoadImage(const std::string& path);
    Anim LoadAnim(const std::string& path);

    void ClearCache();

private:
    AssetLoader loader;
    std::unordered_map<std::string, std::weak_ptr<Model>> modelCache;
    std::unordered_map<std::string, std::weak_ptr<Image>> imageCache;
};

}
