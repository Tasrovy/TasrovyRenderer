#include "Scene.h"
#include "Object.h"
#include "Light.h"
#include "Camera.h"
#include <algorithm>

namespace Tasrovy::Render {

std::shared_ptr<Scene> Scene::create(const std::string& name) {
    auto scene = std::shared_ptr<Scene>(new Scene());
    scene->name_ = name;
    return scene;
}

Scene::~Scene() = default;

std::shared_ptr<Scene> Scene::clone() const {
    auto scene = Scene::create(name_);

    for (const auto& object : objects_) {
        if (object) {
            scene->addObject(object->clone());
        }
    }

    for (const auto& light : lights_) {
        if (light) {
            scene->addLight(light->clone());
        }
    }

    for (const auto& camera : cameras_) {
        if (!camera) {
            continue;
        }
        auto clonedCamera = camera->clone();
        auto* clonedCameraPtr = clonedCamera.get();
        scene->addCamera(std::move(clonedCamera));
        if (camera.get() == primaryCamera_) {
            scene->setPrimaryCamera(clonedCameraPtr);
        }
    }

    return scene;
}

void Scene::setName(const std::string& name) { name_ = name; }
const std::string& Scene::getName() const { return name_; }

// --- Add ---

void Scene::addObject(std::shared_ptr<Object> object) {
    objects_.push_back(std::move(object));
}

void Scene::addLight(std::unique_ptr<Light> light) {
    lights_.push_back(std::move(light));
}

void Scene::addCamera(std::unique_ptr<Camera> camera) {
    cameras_.push_back(std::move(camera));
}

// --- Remove ---

void Scene::removeObject(Object* object) {
    auto it = std::find_if(objects_.begin(), objects_.end(),
        [object](const std::shared_ptr<Object>& ptr) { return ptr.get() == object; });
    if (it != objects_.end()) {
        objects_.erase(it);
    }
}

void Scene::removeLight(Light* light) {
    auto it = std::find_if(lights_.begin(), lights_.end(),
        [light](const std::unique_ptr<Light>& ptr) { return ptr.get() == light; });
    if (it != lights_.end()) {
        lights_.erase(it);
    }
}

void Scene::removeCamera(Camera* camera) {
    auto it = std::find_if(cameras_.begin(), cameras_.end(),
        [camera](const std::unique_ptr<Camera>& ptr) { return ptr.get() == camera; });
    if (it != cameras_.end()) {
        if (primaryCamera_ == camera) {
            primaryCamera_ = nullptr;
        }
        cameras_.erase(it);
    }
}

// --- Find by name ---

Object* Scene::findObject(const std::string& name) const {
    for (const auto& obj : objects_) {
        if (obj->getName() == name) {
            return obj.get();
        }
    }
    return nullptr;
}

Light* Scene::findLight(const std::string& name) const {
    for (const auto& light : lights_) {
        if (light->getName() == name) {
            return light.get();
        }
    }
    return nullptr;
}

Camera* Scene::findCamera(const std::string& name) const {
    for (const auto& cam : cameras_) {
        if (cam->getName() == name) {
            return cam.get();
        }
    }
    return nullptr;
}

// --- Find with predicate ---

Object* Scene::findObjectIf(std::function<bool(const Object&)> predicate) const {
    for (const auto& obj : objects_) {
        if (predicate(*obj)) {
            return obj.get();
        }
    }
    return nullptr;
}

std::vector<Object*> Scene::findObjectsIf(std::function<bool(const Object&)> predicate) const {
    std::vector<Object*> result;
    for (const auto& obj : objects_) {
        if (predicate(*obj)) {
            result.push_back(obj.get());
        }
    }
    return result;
}

// --- Get by index ---

std::shared_ptr<Object> Scene::getObject(size_t index) const {
    return index < objects_.size() ? objects_[index] : nullptr;
}

Light* Scene::getLight(size_t index) const {
    return index < lights_.size() ? lights_[index].get() : nullptr;
}

Camera* Scene::getCamera(size_t index) const {
    return index < cameras_.size() ? cameras_[index].get() : nullptr;
}

// --- Count ---

size_t Scene::getObjectCount() const { return objects_.size(); }
size_t Scene::getLightCount() const { return lights_.size(); }
size_t Scene::getCameraCount() const { return cameras_.size(); }

// --- Primary camera ---

Camera* Scene::getPrimaryCamera() const { return primaryCamera_; }

void Scene::setPrimaryCamera(Camera* camera) { primaryCamera_ = camera; }

// --- Iteration ---

const std::vector<std::shared_ptr<Object>>& Scene::getObjects() const { return objects_; }
const std::vector<std::unique_ptr<Light>>& Scene::getLights() const { return lights_; }
const std::vector<std::unique_ptr<Camera>>& Scene::getCameras() const { return cameras_; }

// --- Utility ---

void Scene::clear() {
    objects_.clear();
    lights_.clear();
    cameras_.clear();
    primaryCamera_ = nullptr;
}

const UniformData Scene::GenUniformData() const{
    UniformData data;
    std::vector<TSMat4f> modelMatrix;
    for (const auto& obj : objects_) {
        modelMatrix.push_back(obj->getModelMatrix());
    }
    data.modelMatrix = modelMatrix;
    data.primaryCamera = primaryCamera_;
    for (const auto& l : lights_) {
        data.lights.push_back(l.get());
    }
    return data;
}

} // namespace Tasrovy::Render
