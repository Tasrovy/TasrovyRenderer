#pragma once

#include "TSVector.h"

#include <cstdint>
#include <string>

namespace Tasrovy::Render {

enum class MaterialTextureColorSpace : uint8_t {
    Linear,
    SRGB
};

// UV orientation belongs to a texture binding, not to the mesh/submesh. This
// lets images authored with different coordinate conventions share one mesh.
enum class MaterialTextureUvMode : uint8_t {
    Identity = 0,
    FlipY = 1,
    FlipX = 2,
    FlipXY = 3,
    SwapXY = 4,
    SwapXYFlipY = 5,
    SwapXYFlipX = 6
};

struct MaterialTextureUvSampling {
    MaterialTextureUvMode mode = MaterialTextureUvMode::Identity;
    TSVec2f scale = TSVec2f(1.0f);
    TSVec2f offset = TSVec2f(0.0f);
};

struct MaterialTextureDefault {
    std::string name;
    TSVec4f color = TSVec4f(1.0f);
    MaterialTextureColorSpace colorSpace = MaterialTextureColorSpace::Linear;
};

struct MaterialTextureRequirement {
    std::string slot;
    uint32_t binding = 0;
    MaterialTextureColorSpace colorSpace = MaterialTextureColorSpace::Linear;
    std::string defaultTexture;
    TSVec4f defaultColor = TSVec4f(1.0f);
};

} // namespace Tasrovy::Render
