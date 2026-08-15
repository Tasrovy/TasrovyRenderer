#include "MaterialDescriptor.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Tasrovy::Render {

namespace {

using nlohmann::json;

TSVec2f parseVec2(
    const json& value,
    const TSVec2f& fallback) {
    if (!value.is_array() || value.size() != 2) {
        return fallback;
    }
    return TSVec2f(value.at(0).get<float>(), value.at(1).get<float>());
}

MaterialTextureUvMode parseTextureUvMode(const std::string& mode) {
    if (mode == "identity" || mode == "uv0") {
        return MaterialTextureUvMode::Identity;
    }
    if (mode == "flipY") return MaterialTextureUvMode::FlipY;
    if (mode == "flipX") return MaterialTextureUvMode::FlipX;
    if (mode == "flipXY") return MaterialTextureUvMode::FlipXY;
    if (mode == "swapXY") return MaterialTextureUvMode::SwapXY;
    if (mode == "swapXYFlipY") return MaterialTextureUvMode::SwapXYFlipY;
    if (mode == "swapXYFlipX") return MaterialTextureUvMode::SwapXYFlipX;
    throw std::runtime_error("unsupported texture UV mode: " + mode);
}

TSVec3f parseVec3(const json& value) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error("material vec3 parameter requires 3 components");
    }
    return TSVec3f(
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>());
}

TSVec4f parseVec4(const json& value) {
    if (!value.is_array() || value.size() != 4) {
        throw std::runtime_error("material vec4 parameter requires 4 components");
    }
    return TSVec4f(
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
        value.at(3).get<float>());
}

MaterialPropertyType parsePropertyType(const std::string& type) {
    if (type == "float") {
        return MaterialPropertyType::Float;
    }
    if (type == "float3") {
        return MaterialPropertyType::Float3;
    }
    if (type == "float4") {
        return MaterialPropertyType::Float4;
    }
    if (type == "texture2D") {
        return MaterialPropertyType::Texture2D;
    }
    throw std::runtime_error("unsupported material property type: " + type);
}

} // namespace

std::shared_ptr<MaterialDescriptor> MaterialDescriptor::load(
    const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error(
            "failed to open material descriptor: " + path.string());
    }
    json root;
    stream >> root;

    auto descriptor =
        std::shared_ptr<MaterialDescriptor>(new MaterialDescriptor());
    descriptor->sourcePath_ = path;
    descriptor->name_ = root.value("name", path.stem().string());
    descriptor->castShadows_ = root.value("castShadows", true);
    descriptor->alphaCutoff_ = root.value("alphaCutoff", 0.5f);
    descriptor->surface_ = root.value("surface", 0u);

    const auto& properties = root.at("properties");
    if (!properties.is_array()) {
        throw std::runtime_error(
            "material descriptor 'properties' must be an array: " +
            path.string());
    }
    for (const auto& property : properties) {
        const auto name = property.at("name").get<std::string>();
        if (name.empty() || descriptor->propertyIndices_.contains(name)) {
            throw std::runtime_error(
                "material property names must be non-empty and unique: " +
                path.string());
        }
        const auto type =
            parsePropertyType(property.at("type").get<std::string>());
        const auto index = descriptor->properties_.size();
        descriptor->properties_.push_back({name, type});
        descriptor->propertyIndices_.emplace(name, index);

        const auto& value = property.at("value");
        switch (type) {
        case MaterialPropertyType::Float:
            descriptor->floats_[name] = value.get<float>();
            break;
        case MaterialPropertyType::Float3:
            descriptor->vec3s_[name] = parseVec3(value);
            break;
        case MaterialPropertyType::Float4:
            descriptor->vec4s_[name] = parseVec4(value);
            break;
        case MaterialPropertyType::Texture2D:
            if (value.is_string()) {
                descriptor->texturePaths_[name] = value.get<std::string>();
                descriptor->textureSampling_[name] = {};
                break;
            }
            if (!value.is_object()) {
                throw std::runtime_error(
                    "material texture2D value must be a path string or object");
            }
            descriptor->texturePaths_[name] =
                value.value("path", std::string());
            descriptor->textureSampling_[name] = {
                parseTextureUvMode(value.value("uvMode", std::string("identity"))),
                parseVec2(value.value("scale", json::array()), TSVec2f(1.0f)),
                parseVec2(value.value("offset", json::array()), TSVec2f(0.0f))
            };
            break;
        }
    }
    return descriptor;
}

const std::string& MaterialDescriptor::getName() const { return name_; }
const std::filesystem::path& MaterialDescriptor::getSourcePath() const { return sourcePath_; }
const std::vector<MaterialPropertyDeclaration>&
MaterialDescriptor::getProperties() const {
    return properties_;
}
const MaterialPropertyDeclaration& MaterialDescriptor::requireProperty(
    const std::string& name) const {
    const auto found = propertyIndices_.find(name);
    if (found == propertyIndices_.end()) {
        throw std::invalid_argument(
            "material property is not declared: " + name);
    }
    return properties_[found->second];
}
const std::unordered_map<std::string, float>& MaterialDescriptor::getFloatParams() const { return floats_; }
const std::unordered_map<std::string, TSVec3f>& MaterialDescriptor::getVec3Params() const { return vec3s_; }
const std::unordered_map<std::string, TSVec4f>& MaterialDescriptor::getVec4Params() const { return vec4s_; }
const std::unordered_map<std::string, std::string>& MaterialDescriptor::getTexturePaths() const { return texturePaths_; }
const std::unordered_map<std::string, MaterialTextureUvSampling>&
MaterialDescriptor::getTextureSampling() const {
    return textureSampling_;
}
bool MaterialDescriptor::castsShadows() const { return castShadows_; }
float MaterialDescriptor::getAlphaCutoff() const { return alphaCutoff_; }
uint32_t MaterialDescriptor::getSurface() const { return surface_; }

} // namespace Tasrovy::Render
