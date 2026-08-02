#pragma once

#include "TSVector.h"

#include <cstdint>
#include <string>

namespace Tasrovy::Render {

enum class MaterialTextureColorSpace : uint8_t {
    Linear,
    SRGB
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
