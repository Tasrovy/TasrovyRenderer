#include "MaterialTechnique.h"

#include "Shader.h"

#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace Tasrovy::Render {
namespace {

using nlohmann::json;

MaterialCategory parseCategory(const std::string& value) {
    if (value == "scene") return MaterialCategory::Scene;
    if (value == "vegetation") return MaterialCategory::Vegetation;
    if (value == "character") return MaterialCategory::Character;
    if (value == "special") return MaterialCategory::Special;
    throw std::runtime_error("unsupported material technique category: " + value);
}

std::filesystem::path resolveAssetPath(
    const std::filesystem::path& owner,
    const std::filesystem::path& value) {
    if (value.empty() || value.is_absolute() || std::filesystem::exists(value)) {
        return value;
    }
    return (owner.parent_path() / value).lexically_normal();
}

std::shared_ptr<Shader> parseShaderStage(
    const json& value,
    ShaderType stage,
    const std::filesystem::path& techniquePath) {
    if (value.is_null()) return nullptr;

    std::filesystem::path source;
    std::string entry;
    std::optional<uint64_t> permutation;
    if (value.is_string()) {
        source = value.get<std::string>();
    } else if (value.is_object()) {
        source = value.value("source", std::string());
        entry = value.value("entry", std::string());
        if (value.contains("permutation")) {
            permutation = value.at("permutation").get<uint64_t>();
        }
    } else {
        throw std::runtime_error(
            "material shader stage must be a source path or object");
    }
    if (source.empty()) {
        throw std::runtime_error("material shader source must not be empty");
    }
    auto shader = Shader::create(
        resolveAssetPath(techniquePath, source).generic_string(),
        stage,
        permutation);
    if (!entry.empty()) shader->setEntry(entry);
    return shader;
}

} // namespace

std::shared_ptr<MaterialTechnique> MaterialTechnique::load(
    const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error(
            "failed to open material technique: " + path.string());
    }
    json root;
    stream >> root;

    auto technique =
        std::shared_ptr<MaterialTechnique>(new MaterialTechnique());
    technique->sourcePath_ = path;
    technique->name_ = root.value("name", path.stem().string());
    technique->category_ = parseCategory(
        root.value("category", std::string("scene")));

    const auto variants = root.value("variants", json::object());
    if (!variants.is_object()) {
        throw std::runtime_error(
            "material technique 'variants' must be an object: " +
            path.string());
    }
    for (const auto& [slot, value] : variants.items()) {
        if (slot.empty() || !value.is_object()) {
            throw std::runtime_error(
                "material technique variants require named objects: " +
                path.string());
        }
        MaterialShader shader;
        if (value.contains("vertex")) {
            shader.vertexShader = parseShaderStage(
                value.at("vertex"), ShaderType::Vertex, path);
        }
        if (value.contains("fragment")) {
            shader.fragmentShader = parseShaderStage(
                value.at("fragment"), ShaderType::Fragment, path);
        }
        if (shader.empty()) {
            throw std::runtime_error(
                "material technique variant must define a shader stage: " +
                slot);
        }
        technique->shaders_.emplace(slot, std::move(shader));
    }
    return technique;
}

const std::string& MaterialTechnique::getName() const { return name_; }
const std::filesystem::path& MaterialTechnique::getSourcePath() const {
    return sourcePath_;
}
MaterialCategory MaterialTechnique::getCategory() const { return category_; }
const MaterialShader* MaterialTechnique::findShader(
    const std::string& slot) const {
    const auto found = shaders_.find(slot);
    return found == shaders_.end() ? nullptr : &found->second;
}
const std::unordered_map<std::string, MaterialShader>&
MaterialTechnique::getShaders() const {
    return shaders_;
}

} // namespace Tasrovy::Render
