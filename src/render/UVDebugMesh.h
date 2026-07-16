#pragma once

#include "Mesh.h"
#include <memory>

namespace Tasrovy::Render {

class UVDebugMesh {
public:
    static std::shared_ptr<Mesh> createFromMesh(const Mesh& source, float panelSpacing = 2.35f);

private:
    UVDebugMesh() = default;
};

} // namespace Tasrovy::Render
