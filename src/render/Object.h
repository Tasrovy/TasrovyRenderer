#pragma once

#include "Transform.h"
#include <string>
#include <vector>
#include <memory>

namespace Tasrovy {

class Mesh;
class Material;

class Object : public std::enable_shared_from_this<Object> {
public:
    static std::shared_ptr<Object> create();
    static std::shared_ptr<Object> create(const std::string& name);
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    Transform& getTransform();
    const Transform& getTransform() const;

    void setPosition(TSVec3f pos);
    void setRotation(TSVec3f euler);
    void setRotation(TSQuatf quat);
    void setScale(TSVec3f s);

    TSVec3f getPosition() const;
    TSVec3f getRotationEuler() const;
    TSQuatf getRotationQuat() const;
    TSVec3f getScale() const;

    void setMesh(std::weak_ptr<Mesh> mesh);
    std::shared_ptr<Mesh> getMesh() const;

    void setMaterial(std::weak_ptr<Material> material);
    std::shared_ptr<Material> getMaterial() const;

    void setName(const std::string& name);
    const std::string& getName() const;

    void setActive(bool active);
    bool isActive() const;

    void addChild(std::shared_ptr<Object> child);
    void removeChild(Object* child);
    const std::vector<std::shared_ptr<Object>>& getChildren() const;
    std::shared_ptr<Object> getParent() const;

    virtual TSMat4f getModelMatrix() const;

private:
    Object() = default;

protected:
    explicit Object(const std::string& name);

    std::string name_;
    Transform transform_;

    std::weak_ptr<Mesh> mesh_;
    std::weak_ptr<Material> material_;
    bool active_ = true;

    std::weak_ptr<Object> parent_;
    std::vector<std::shared_ptr<Object>> children_;
};

} // namespace Tasrovy
