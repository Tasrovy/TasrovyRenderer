#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Tasrovy {

class Object;
class Light;
class Camera;

class Scene {
public:
    static std::unique_ptr<Scene> create(const std::string& name = "");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void setName(const std::string& name);
    const std::string& getName() const;

    // --- Add ---
    void addObject(std::shared_ptr<Object> object);
    void addLight(std::unique_ptr<Light> light);
    void addCamera(std::unique_ptr<Camera> camera);

    // --- Remove ---
    void removeObject(Object* object);
    void removeLight(Light* light);
    void removeCamera(Camera* camera);

    // --- Find by name ---
    Object* findObject(const std::string& name) const;
    Light* findLight(const std::string& name) const;
    Camera* findCamera(const std::string& name) const;

    // --- Find first满足条件 ---
    Object* findObjectIf(std::function<bool(const Object&)> predicate) const;
    std::vector<Object*> findObjectsIf(std::function<bool(const Object&)> predicate) const;

    // --- Get by index ---
    std::shared_ptr<Object> getObject(size_t index) const;
    Light* getLight(size_t index) const;
    Camera* getCamera(size_t index) const;

    // --- Count ---
    size_t getObjectCount() const;
    size_t getLightCount() const;
    size_t getCameraCount() const;

    // --- Primary camera ---
    Camera* getPrimaryCamera() const;
    void setPrimaryCamera(Camera* camera);

    // --- Iteration ---
    const std::vector<std::shared_ptr<Object>>& getObjects() const;
    const std::vector<std::unique_ptr<Light>>& getLights() const;
    const std::vector<std::unique_ptr<Camera>>& getCameras() const;

    // --- Utility ---
    void clear();

private:
    Scene() = default;

    std::string name_;
    std::vector<std::shared_ptr<Object>> objects_;
    std::vector<std::unique_ptr<Light>> lights_;
    std::vector<std::unique_ptr<Camera>> cameras_;
    Camera* primaryCamera_ = nullptr;
};

} // namespace Tasrovy
