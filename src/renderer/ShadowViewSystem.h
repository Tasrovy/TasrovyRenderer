#pragma once

#include "TSMatrix.h"
#include "TSVector.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Tasrovy::Render {
class Camera;
}

namespace Tasrovy::Renderer {

inline constexpr size_t ShadowCascadeCount = 4;
inline constexpr uint32_t ShadowMapResolution = 2048;
inline constexpr uint32_t VirtualShadowAtlasResolution = 4096;
inline constexpr uint32_t VirtualShadowPageResolution = 2048;

// API-independent shadow-view data consumed by frame-parameter construction.
// This system only computes logical light views and virtual-page mappings; it
// never allocates an RHI image or records commands.
struct ShadowViewData {
    std::array<Tasrovy::Base::TSMat4f, ShadowCascadeCount> views{};
    std::array<Tasrovy::Base::TSMat4f, ShadowCascadeCount> projections{};
    std::array<Tasrovy::Base::TSMat4f, ShadowCascadeCount> viewProjections{};
    std::array<float, ShadowCascadeCount> splits{};
    std::array<Tasrovy::Base::TSVec4f, ShadowCascadeCount> virtualPageTable{};
};

class ShadowViewSystem {
public:
    ShadowViewData build(
        const Tasrovy::Render::Camera& camera,
        const Tasrovy::Base::TSVec3f& lightDirection,
        float maximumDistance,
        float splitLambda,
        bool cascadesEnabled) const;
};

} // namespace Tasrovy::Renderer
