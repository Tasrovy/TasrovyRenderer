#include "FileSystem.hpp"

namespace Tasrovy::FS {

std::shared_ptr<Model> FileSystem::LoadModel(const std::string& path) {
    auto it = modelCache.find(path);
    if (it != modelCache.end()) {
        if (auto ptr = it->second.lock()) {
            return ptr;
        }
    }

    auto model = loader.LoadModel(path);
    if (model) {
        modelCache[path] = model;
    }
    return model;
}

std::shared_ptr<Image> FileSystem::LoadImage(const std::string& path) {
    auto it = imageCache.find(path);
    if (it != imageCache.end()) {
        if (auto ptr = it->second.lock()) {
            return ptr;
        }
    }

    auto image = std::make_shared<Image>();
    if (image->LoadFromFile(path)) {
        imageCache[path] = image;
        return image;
    }
    return nullptr;
}

Anim FileSystem::LoadAnim(const std::string& path) {
    return loader.LoadAnim(path);
}

void FileSystem::ClearCache() {
    modelCache.clear();
    imageCache.clear();
}

}
