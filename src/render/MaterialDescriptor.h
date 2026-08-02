#pragma once

#include "TSVector.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::Render {

enum class MaterialPropertyType : uint8_t {
    Float,
    Float3,
    Float4,
    Texture2D
};

struct MaterialPropertyDeclaration {
    std::string name;
    MaterialPropertyType type = MaterialPropertyType::Float;
};

class MaterialDescriptor {
public:
    static std::shared_ptr<MaterialDescriptor> load(const std::filesystem::path& path);

    const std::string& getName() const;
    const std::filesystem::path& getSourcePath() const;
    const std::vector<MaterialPropertyDeclaration>& getProperties() const;
    const MaterialPropertyDeclaration& requireProperty(
        const std::string& name) const;
    const std::unordered_map<std::string, float>& getFloatParams() const;
    const std::unordered_map<std::string, TSVec3f>& getVec3Params() const;
    const std::unordered_map<std::string, TSVec4f>& getVec4Params() const;
    const std::unordered_map<std::string, std::string>& getTexturePaths() const;
    bool castsShadows() const;
    float getAlphaCutoff() const;
    uint32_t getSurface() const;

private:
    std::string name_;
    std::filesystem::path sourcePath_;
    std::vector<MaterialPropertyDeclaration> properties_;
    std::unordered_map<std::string, size_t> propertyIndices_;
    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, TSVec3f> vec3s_;
    std::unordered_map<std::string, TSVec4f> vec4s_;
    std::unordered_map<std::string, std::string> texturePaths_;
    bool castShadows_ = true;
    float alphaCutoff_ = 0.5f;
    uint32_t surface_ = 0;
};

} // namespace Tasrovy::Render
