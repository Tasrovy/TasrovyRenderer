#include "Material.h"
#include "Shader.h"
#include <Logger.hpp>

namespace Tasrovy {

Material::Material() {
    LOG_INFO("Material ctor");
}

std::shared_ptr<Material> Material::create() {
    LOG_INFO("Material::create before new");
    auto m = std::shared_ptr<Material>(new Material());
    LOG_INFO("Material::create after new");
    return m;
}

std::shared_ptr<Material> Material::create(std::weak_ptr<Shader> shader) {
    auto mat = std::shared_ptr<Material>(new Material());
    mat->shader_ = shader;
    return mat;
}

void Material::setShader(std::weak_ptr<Shader> shader) { shader_ = shader; }

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
    reflectedUniforms_ = std::move(uniforms);
}

const std::vector<Material::ReflectedUniform>& Material::getReflectedUniforms() const {
    return reflectedUniforms_;
}

} // namespace Tasrovy
