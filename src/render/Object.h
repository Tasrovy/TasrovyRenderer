#pragma once

#include "Transform.h"
#include <string>
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>

namespace Tasrovy::Render {

class Mesh;
class Material;

class Object : public std::enable_shared_from_this<Object> {
public:
    static std::shared_ptr<Object> create();
    static std::shared_ptr<Object> create(const std::string& name);
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    virtual std::shared_ptr<Object> clone() const;

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
    void setSubmeshMaterial(size_t submeshIndex, std::weak_ptr<Material> material);
    std::shared_ptr<Material> getSubmeshMaterial(size_t submeshIndex) const;
    void clearSubmeshMaterials();

    void setName(const std::string& name);
    const std::string& getName() const;

    // Persistent render identity. It is copied into immutable scene snapshots,
    // so render-thread GPUScene indices do not depend on Object addresses.
    uint64_t getRenderId() const;

    void setActive(bool active);
    bool isActive() const;

    void setFlipProjectionY(bool flipProjectionY);
    bool getFlipProjectionY() const;

    void addChild(std::shared_ptr<Object> child);
    void removeChild(Object* child);
    const std::vector<std::shared_ptr<Object>>& getChildren() const;
    std::shared_ptr<Object> getParent() const;

    virtual TSMat4f getModelMatrix() const;

private:
    Object();

protected:
    explicit Object(const std::string& name);

    std::string name_;
    uint64_t renderId_ = 0;
    Transform transform_;

    std::weak_ptr<Mesh> mesh_;
    std::weak_ptr<Material> material_;
    std::shared_ptr<Mesh> meshKeepAlive_;
    std::shared_ptr<Material> materialKeepAlive_;
    bool active_ = true;
    bool flipProjectionY_ = true;

    std::weak_ptr<Object> parent_;
    std::vector<std::shared_ptr<Object>> children_;
};

} // namespace Tasrovy::Render
