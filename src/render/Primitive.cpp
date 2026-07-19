#include "Primitive.h"

#include "Mesh.h"
#include "Model.hpp"
#include <algorithm>
#include <unordered_map>

namespace Tasrovy::Render {

namespace {

std::shared_ptr<Mesh> planeMesh() {
    static const auto mesh = [] {
        const auto model = Tasrovy::FS::Model::GenPlane();
        return Mesh::fromModel(*model);
    }();
    return mesh;
}

std::shared_ptr<Mesh> cubeMesh() {
    static const auto mesh = [] {
        const auto model = Tasrovy::FS::Model::GenCube();
        return Mesh::fromModel(*model);
    }();
    return mesh;
}

std::shared_ptr<Mesh> sphereMesh(uint32_t sectors, uint32_t stacks) {
    sectors = std::max(3u, sectors);
    stacks = std::max(2u, stacks);
    const uint64_t key = (static_cast<uint64_t>(sectors) << 32u) | stacks;
    static std::unordered_map<uint64_t, std::weak_ptr<Mesh>> cache;
    if (const auto found = cache.find(key); found != cache.end()) {
        if (auto mesh = found->second.lock()) {
            return mesh;
        }
    }
    const auto model = Tasrovy::FS::Model::GenSphere(sectors, stacks);
    auto mesh = Mesh::fromModel(*model);
    cache[key] = mesh;
    return mesh;
}

void copyObjectState(const Object& source, const std::shared_ptr<Object>& destination) {
    destination->setPosition(source.getPosition());
    destination->setRotation(source.getRotationQuat());
    destination->setScale(source.getScale());
    destination->setMaterial(source.getMaterial());
    destination->setActive(source.isActive());
    destination->setFlipProjectionY(source.getFlipProjectionY());
    for (const auto& child : source.getChildren()) {
        if (child) {
            destination->addChild(child->clone());
        }
    }
}

} // namespace

Plane::Plane(const std::string& name) : Object(name) {
    meshKeepAlive_ = planeMesh();
    mesh_ = meshKeepAlive_;
}

std::shared_ptr<Plane> Plane::create(const std::string& name) {
    return std::shared_ptr<Plane>(new Plane(name));
}

std::shared_ptr<Object> Plane::clone() const {
    auto result = Plane::create(name_);
    copyObjectState(*this, result);
    result->materialKeepAlive_ = getMaterial();
    result->material_ = result->materialKeepAlive_;
    return result;
}

Cube::Cube(const std::string& name) : Object(name) {
    meshKeepAlive_ = cubeMesh();
    mesh_ = meshKeepAlive_;
}

std::shared_ptr<Cube> Cube::create(const std::string& name) {
    return std::shared_ptr<Cube>(new Cube(name));
}

std::shared_ptr<Object> Cube::clone() const {
    auto result = Cube::create(name_);
    copyObjectState(*this, result);
    result->materialKeepAlive_ = getMaterial();
    result->material_ = result->materialKeepAlive_;
    return result;
}

Sphere::Sphere(const std::string& name, uint32_t sectors, uint32_t stacks)
    : Object(name), sectors_(std::max(3u, sectors)), stacks_(std::max(2u, stacks)) {
    meshKeepAlive_ = sphereMesh(sectors_, stacks_);
    mesh_ = meshKeepAlive_;
}

std::shared_ptr<Sphere> Sphere::create(
    const std::string& name,
    uint32_t sectors,
    uint32_t stacks) {
    return std::shared_ptr<Sphere>(new Sphere(name, sectors, stacks));
}

std::shared_ptr<Object> Sphere::clone() const {
    auto result = Sphere::create(name_, sectors_, stacks_);
    copyObjectState(*this, result);
    result->materialKeepAlive_ = getMaterial();
    result->material_ = result->materialKeepAlive_;
    return result;
}

} // namespace Tasrovy::Render
