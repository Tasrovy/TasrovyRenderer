#include "Material.h"
#include "Shader.h"

namespace Tasrovy {

Material::Material() = default;

std::shared_ptr<Material> Material::create() {
    return std::shared_ptr<Material>(new Material());
}

std::shared_ptr<Material> Material::create(std::weak_ptr<Shader> shader) {
    auto mat = std::shared_ptr<Material>(new Material());
    mat->shader_ = shader;
    return mat;
}

void Material::setShader(std::weak_ptr<Shader> shader) {
    shader_ = shader;
    reflectionPending_.store(true, std::memory_order_release);
}

std::shared_ptr<Shader> Material::getShader() const { return shader_.lock(); }

void Material::setFloat(const std::string& name, float value) { floats_[name] = value; }
void Material::setVec3(const std::string& name, TSVec3f value) { vec3s_[name] = value; }
void Material::setVec4(const std::string& name, TSVec4f value) { vec4s_[name] = value; }
void Material::setMat4(const std::string& name, TSMat4f value) { mat4s_[name] = value; }

void Material::setTexture(const std::string& samplerName, const std::string& texturePath) {
    textures_[samplerName] = { 0, texturePath };
}

void Material::setTexture(const std::string& samplerName, uint32_t binding, const std::string& texturePath) {
    textures_[samplerName] = { binding, texturePath };
}

float Material::getFloat(const std::string& name, float fallback) const {
    auto it = floats_.find(name);
    return it != floats_.end() ? it->second : fallback;
}

TSVec3f Material::getVec3(const std::string& name, TSVec3f fallback) const {
    auto it = vec3s_.find(name);
    return it != vec3s_.end() ? it->second : fallback;
}

TSVec4f Material::getVec4(const std::string& name, TSVec4f fallback) const {
    auto it = vec4s_.find(name);
    return it != vec4s_.end() ? it->second : fallback;
}

TSMat4f Material::getMat4(const std::string& name) const {
    auto it = mat4s_.find(name);
    return it != mat4s_.end() ? it->second : TSMat4f(1.0f);
}

std::string Material::getTexture(const std::string& samplerName) const {
    auto it = textures_.find(samplerName);
    return it != textures_.end() ? it->second.path : "";
}

bool Material::hasFloat(const std::string& name) const { return floats_.count(name) > 0; }
bool Material::hasVec3(const std::string& name) const { return vec3s_.count(name) > 0; }
bool Material::hasTexture(const std::string& samplerName) const { return textures_.count(samplerName) > 0; }

const std::unordered_map<std::string, float>& Material::getFloatParams() const { return floats_; }
const std::unordered_map<std::string, TSVec3f>& Material::getVec3Params() const { return vec3s_; }
const std::unordered_map<std::string, TSVec4f>& Material::getVec4Params() const { return vec4s_; }
const std::unordered_map<std::string, TSMat4f>& Material::getMat4Params() const { return mat4s_; }
const std::unordered_map<std::string, Material::TextureBinding>& Material::getTextureBindings() const { return textures_; }

void Material::setReflectedUniforms(std::vector<ReflectedUniform> uniforms) {
    std::lock_guard lock(reflectionMutex_);
    reflectedUniforms_ = std::move(uniforms);
}

const std::vector<Material::ReflectedUniform>& Material::getReflectedUniforms() const {
    return reflectedUniforms_;
}

const std::vector<Material::ReflectedSamplerBinding>& Material::getReflectedSamplers() const {
    return reflectedSamplers_;
}

bool Material::isReflectionPending() const {
    return reflectionPending_.load(std::memory_order_acquire);
}

void Material::applyReflection(const ShaderReflectionData& vertData, const ShaderReflectionData& fragData) {
    std::lock_guard lock(reflectionMutex_);

    // Build flat uniform list from both stages
    std::vector<ReflectedUniform> allUniforms;

    for (auto& block : vertData.uniformBlocks) {
        for (auto& member : block.members) {
            allUniforms.push_back({member.name, member.offset, member.size,
                                   static_cast<uint32_t>(member.type)});
        }
    }
    for (auto& block : fragData.uniformBlocks) {
        for (auto& member : block.members) {
            allUniforms.push_back({member.name, member.offset, member.size,
                                   static_cast<uint32_t>(member.type)});
        }
    }
    reflectedUniforms_ = std::move(allUniforms);

    // Merge samplers from both stages
    reflectedSamplers_.clear();
    for (auto& s : vertData.samplers) {
        reflectedSamplers_.push_back(s);
    }
    for (auto& s : fragData.samplers) {
        reflectedSamplers_.push_back(s);
    }

    reflectionPending_.store(false, std::memory_order_release);
}

} // namespace Tasrovy
