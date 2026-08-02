#pragma once

#include "TSQuaternion.h"
#include "TSVector.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Tasrovy::Render {
class Material;
class Mesh;
class Object;
}

namespace Tasrovy::Renderer {

// Persistent render-thread representation of one renderable primitive. It
// contains only state required to update the render scene and never exposes a
// game-thread Object to the executor.
struct PrimitiveSceneProxy {
    uint64_t id = 0;
    std::string name;
    Tasrovy::Base::TSVec3f position = Tasrovy::Base::TSVec3f(0.0f);
    Tasrovy::Base::TSQuatf rotation =
        Tasrovy::Base::TSQuatf(1.0f, 0.0f, 0.0f, 0.0f);
    Tasrovy::Base::TSVec3f scale = Tasrovy::Base::TSVec3f(1.0f);
    std::shared_ptr<Tasrovy::Render::Mesh> mesh;
    std::shared_ptr<Tasrovy::Render::Material> material;
    std::vector<std::shared_ptr<Tasrovy::Render::Material>>
        submeshMaterials;
    bool active = true;
    bool flipProjectionY = true;

    static PrimitiveSceneProxy fromObject(
        const Tasrovy::Render::Object& object);
};

} // namespace Tasrovy::Renderer
