#include "PrimitiveSceneProxy.h"

#include "../render/Mesh.h"
#include "../render/Object.h"

namespace Tasrovy::Renderer {
PrimitiveSceneProxy PrimitiveSceneProxy::fromObject(
    const Tasrovy::Render::Object& object) {
    PrimitiveSceneProxy proxy;
    proxy.id = object.getRenderId();
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
