#include "SkyboxGeometry.h"

namespace Tasrovy::Renderer {

const std::vector<SkyboxVertexData>& getSkyboxVertices() {
    static const std::vector<SkyboxVertexData> vertices = {
        {{-1.0f, -1.0f,  1.0f}}, {{ 1.0f, -1.0f,  1.0f}},
        {{ 1.0f,  1.0f,  1.0f}}, {{-1.0f,  1.0f,  1.0f}},
        {{-1.0f, -1.0f, -1.0f}}, {{ 1.0f, -1.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}}, {{-1.0f,  1.0f, -1.0f}}
    };
    return vertices;
}

const std::vector<uint32_t>& getSkyboxIndices() {
    static const std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0,
        1, 5, 6, 6, 2, 1,
        5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4,
        3, 2, 6, 6, 7, 3,
        4, 5, 1, 1, 0, 4
    };
    return indices;
}

} // namespace Tasrovy::Renderer
