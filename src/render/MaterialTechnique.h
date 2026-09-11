#pragma once

#include "MaterialShader.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Tasrovy::Render {

enum class MaterialCategory : uint8_t {
    Scene,
    Vegetation,
    Character,
    Special
};

namespace MaterialTechniqueSlots {
inline constexpr const char* Shadow = "shadow";
inline constexpr const char* Depth = "depth";
inline constexpr const char* GBuffer = "gbuffer";
inline constexpr const char* Lighting = "lighting";
inline constexpr const char* Forward = "forward";
inline constexpr const char* Transparent = "transparent";
}

class MaterialTechnique {
public:
    static std::shared_ptr<MaterialTechnique> load(
        const std::filesystem::path& path);

    const std::string& getName() const;
    const std::filesystem::path& getSourcePath() const;
    MaterialCategory getCategory() const;
    const MaterialShader* findShader(const std::string& slot) const;
    const std::unordered_map<std::string, MaterialShader>& getShaders() const;

private:
    std::string name_;
    std::filesystem::path sourcePath_;
    MaterialCategory category_ = MaterialCategory::Scene;
    std::unordered_map<std::string, MaterialShader> shaders_;
};

} // namespace Tasrovy::Render
