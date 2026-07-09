#pragma once

#include <cstdint>
#include <string>

namespace Tasrovy::Render {

enum class MaterialTextureSemantic : uint8_t {
    BaseColor,
    Normal,
    MetallicRoughnessAO,
    Emissive,
    Opacity
};

enum class MaterialTextureColorSpace : uint8_t {
    Linear,
    SRGB
};

enum class MaterialTextureFallback : uint8_t {
    White,
    Black,
    FlatNormal,
    MetallicRoughnessAO
};

struct MaterialTextureRequirement {
    MaterialTextureSemantic semantic = MaterialTextureSemantic::BaseColor;
    std::string slot;
    MaterialTextureColorSpace colorSpace = MaterialTextureColorSpace::Linear;
    MaterialTextureFallback fallback = MaterialTextureFallback::White;
};

} // namespace Tasrovy::Render
