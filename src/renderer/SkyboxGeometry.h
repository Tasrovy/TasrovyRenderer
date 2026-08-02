#pragma once

#include "TSVector.h"

#include <cstdint>
#include <vector>

namespace Tasrovy::Renderer {

struct SkyboxVertexData {
    Tasrovy::Base::TSVec3f pos;
};

const std::vector<SkyboxVertexData>& getSkyboxVertices();
const std::vector<uint32_t>& getSkyboxIndices();

} // namespace Tasrovy::Renderer
