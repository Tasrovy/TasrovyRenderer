#include "Object.h"
#include "Material.h"
#include "Mesh.h"
#include <algorithm>

namespace Tasrovy::Render {

std::shared_ptr<Object> Object::create() {
    return std::shared_ptr<Object>(new Object());
}

std::shared_ptr<Object> Object::create(const std::string& name) {
    return std::shared_ptr<Object>(new Object(name));
}

Object::Object(const std::string& name)
    : name_(name) {
}

Object::~Object() = default;

std::shared_ptr<Object> Object::clone() const {
    auto object = Object::create(name_);
    object->transform_ = transform_;
    object->mesh_ = mesh_;
    object->material_ = material_;
    object->active_ = active_;
    for (const auto& child : children_) {
        if (child) {
            object->addChild(child->clone());
        }
    }
    return object;
}

Transform& Object::getTransform() { return transform_; }
const Transform& Object::getTransform() const { return transform_; }

void Object::setPosition(TSVec3f pos) { transform_.setPosition(pos); }
void Object::setRotation(TSVec3f euler) { transform_.setRotation(euler); }
void Object::setRotation(TSQuatf quat) { transform_.setRotation(quat); }
void Object::setScale(TSVec3f s) { transform_.setScale(s); }

TSVec3f Object::getPosition() const { return transform_.getPosition(); }
TSVec3f Object::getRotationEuler() const { return transform_.getRotationEuler(); }
TSQuatf Object::getRotationQuat() const { return transform_.getRotationQuat(); }
TSVec3f Object::getScale() const { return transform_.getScale(); }

void Object::setMesh(std::weak_ptr<Mesh> mesh) { mesh_ = mesh; }
std::shared_ptr<Mesh> Object::getMesh() const { return mesh_.lock(); }

void Object::setMaterial(std::weak_ptr<Material> material) { material_ = material; }
std::shared_ptr<Material> Object::getMaterial() const { return material_.lock(); }

void Object::setName(const std::string& name) { name_ = name; }
const std::string& Object::getName() const { return name_; }

void Object::setActive(bool active) { active_ = active; }
bool Object::isActive() const { return active_; }

void Object::addChild(std::shared_ptr<Object> child) {
    if (auto oldParent = child->parent_.lock()) {
        oldParent->removeChild(child.get());
    }
    child->parent_ = shared_from_this();
    children_.push_back(std::move(child));
}

void Object::removeChild(Object* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const std::shared_ptr<Object>& ptr) { return ptr.get() == child; });
    if (it != children_.end()) {
        (*it)->parent_.reset();
        children_.erase(it);
    }
}

const std::vector<std::shared_ptr<Object>>& Object::getChildren() const { return children_; }

std::shared_ptr<Object> Object::getParent() const { return parent_.lock(); }

TSMat4f Object::getModelMatrix() const {
    return transform_.getModelMatrix();
}

} // namespace Tasrovy::Render
