#include "PrimitiveSceneProxy.h"

#include "../render/Mesh.h"
#include "../render/Object.h"

namespace Tasrovy::Renderer {
namespace {

uint64_t stableNameId(const std::string& name) {
    uint64_t value = 1469598103934665603ull;
    for (const unsigned char character : name) {
        value ^= character;
        value *= 1099511628211ull;
    }
    return value == 0 ? 1 : value;
}

} // namespace

PrimitiveSceneProxy PrimitiveSceneProxy::fromObject(
    const Tasrovy::Render::Object& object) {
    PrimitiveSceneProxy proxy;
    proxy.id = stableNameId(object.getName());
    proxy.name = object.getName();
    proxy.position = object.getPosition();
    proxy.rotation = object.getRotationQuat();
    proxy.scale = object.getScale();
    proxy.mesh = object.getMesh();
    proxy.material = object.getMaterial();
    proxy.active = object.isActive();
    proxy.flipProjectionY = object.getFlipProjectionY();
    if (proxy.mesh) {
        proxy.submeshMaterials.reserve(
            proxy.mesh->getSubmeshes().size());
        for (size_t index = 0;
             index < proxy.mesh->getSubmeshes().size();
             ++index) {
            proxy.submeshMaterials.push_back(
                object.getSubmeshMaterial(index));
        }
    }
    return proxy;
}

} // namespace Tasrovy::Renderer
