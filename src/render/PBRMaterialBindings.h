#pragma once

#include "MaterialTexture.h"

#include <array>
#include <stdexcept>
#include <string>

namespace Tasrovy::Render {

// Descriptor bindings are part of the shader/RHI contract, not material data.
inline const std::array<MaterialTextureRequirement, 4>&
getPBRMaterialTextureBindings() {
    static const std::array<MaterialTextureRequirement, 4> bindings = {{
        {
            "baseColorTexture", 1, MaterialTextureColorSpace::SRGB,
            "White", TSVec4f(1.0f)
        },
        {
            "normalTexture", 2, MaterialTextureColorSpace::Linear,
            "FlatNormal", TSVec4f(0.5f, 0.5f, 1.0f, 1.0f)
        },
        {
            "emissiveTexture", 3, MaterialTextureColorSpace::SRGB,
            "Black", TSVec4f(0.0f, 0.0f, 0.0f, 1.0f)
        },
        {
            "metallicRoughnessAOTexture", 4,
            MaterialTextureColorSpace::Linear,
            "DefaultMRA", TSVec4f(0.0f, 1.0f, 1.0f, 1.0f)
        }
    }};
    return bindings;
}

inline const MaterialTextureRequirement* findPBRMaterialTextureBinding(
    const std::string& name) {
    for (const auto& binding : getPBRMaterialTextureBindings()) {
        if (binding.slot == name) {
            return &binding;
        }
    }
    return nullptr;
}

inline const MaterialTextureRequirement& requirePBRMaterialTextureBinding(
    const std::string& name) {
    if (const auto* binding = findPBRMaterialTextureBinding(name)) {
        return *binding;
    }
    throw std::invalid_argument(
        "unknown PBR material texture property: " + name);
}

} // namespace Tasrovy::Render
