#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Tasrovy::Render {
class Mesh;
}

namespace Tasrovy::Assets {

struct MeshLODSettings {
    // Ratios and errors describe LOD1..N relative to the imported LOD0.
    std::vector<float> triangleRatios = {0.5f, 0.25f, 0.125f};
    std::vector<float> targetErrors = {0.0025f, 0.01f, 0.03f};
    // Projected object diameter / viewport height thresholds for LOD0..N.
    std::vector<float> minimumScreenCoverage = {0.16f, 0.07f, 0.025f, 0.0f};
    uint32_t minimumTriangleCount = 32;
};

class MeshLODGenerator {
public:
    static void generate(
        const std::shared_ptr<Tasrovy::Render::Mesh>& mesh,
        const MeshLODSettings& settings = {});
};

} // namespace Tasrovy::Assets
