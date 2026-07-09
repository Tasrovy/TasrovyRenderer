#include "Material.h"
#include "Shader.h"

namespace Tasrovy::Render {

Material::Material() = default;

std::shared_ptr<Material> Material::create() {
    return std::shared_ptr<Material>(new Material());
}

std::shared_ptr<Material> Material::create(std::weak_ptr<Shader> shader) {
    auto mat = std::shared_ptr<Material>(new Material());
    mat->setShader(shader);
    return mat;
}

std::shared_ptr<Material> Material::create(
    std::weak_ptr<Shader> vertexShader,
    std::weak_ptr<Shader> fragmentShader) {
    auto mat = std::shared_ptr<Material>(new Material());
    mat->setVertexShader(vertexShader);
    mat->setFragmentShader(fragmentShader);
    return mat;
}

void Material::setShader(std::weak_ptr<Shader> shader) {
    const auto shared = shader.lock();
    if (!shared) {
        vertexShader_.reset();
        fragmentShader_.reset();
        reflectionPending_.store(true, std::memory_order_release);
        return;
    }

    switch (shared->getType()) {
    case ShaderType::Vertex:
        vertexShader_ = shader;
        break;
    case ShaderType::Fragment:
        fragmentShader_ = shader;
        break;
    default:
        fragmentShader_ = shader;
        break;
    }
    reflectionPending_.store(true, std::memory_order_release);
}

std::shared_ptr<Shader> Material::getShader() const {
    if (auto fragmentShader = fragmentShader_.lock()) {
        return fragmentShader;
    }
    return vertexShader_.lock();
}

void Material::setVertexShader(std::weak_ptr<Shader> shader) {
    vertexShader_ = shader;
    reflectionPending_.store(true, std::memory_order_release);
}

void Material::setFragmentShader(std::weak_ptr<Shader> shader) {
    fragmentShader_ = shader;
    reflectionPending_.store(true, std::memory_order_release);
}

std::shared_ptr<Shader> Material::getVertexShader() const {
    return vertexShader_.lock();
}

std::shared_ptr<Shader> Material::getFragmentShader() const {
    return fragmentShader_.lock();
}

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

void Material::setTexture(MaterialTextureSemantic semantic, const std::string& texturePath) {
    semanticTextures_[semantic] = {0, texturePath};
}

void Material::setTexture(
    MaterialTextureSemantic semantic,
    uint32_t binding,
    const std::string& texturePath) {
    semanticTextures_[semantic] = {binding, texturePath};
}

void Material::setSurface(MaterialSurface surface) {
    surface_ = surface;
}

MaterialSurface Material::getSurface() const {
    return surface_;
}

void Material::setAlphaCutoff(float alphaCutoff) {
    alphaCutoff_ = alphaCutoff;
}

float Material::getAlphaCutoff() const {
    return alphaCutoff_;
}

void Material::setCastShadows(bool castShadows) {
    castShadows_ = castShadows;
}

bool Material::castsShadows() const {
    return castShadows_;
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

std::string Material::getTexture(MaterialTextureSemantic semantic) const {
    const auto* binding = getTextureBinding(semantic);
    return binding ? binding->path : "";
}

const Material::TextureBinding* Material::getTextureBinding(
    MaterialTextureSemantic semantic) const {
    const auto it = semanticTextures_.find(semantic);
    return it != semanticTextures_.end() ? &it->second : nullptr;
}

const Material::TextureBinding* Material::resolveTexture(
    const MaterialTextureRequirement& requirement) const {
    if (const auto* semanticBinding = getTextureBinding(requirement.semantic)) {
        return semanticBinding;
    }

    const auto namedBinding = textures_.find(requirement.slot);
    return namedBinding != textures_.end() ? &namedBinding->second : nullptr;
}

bool Material::hasFloat(const std::string& name) const { return floats_.count(name) > 0; }
bool Material::hasVec3(const std::string& name) const { return vec3s_.count(name) > 0; }
bool Material::hasTexture(const std::string& samplerName) const { return textures_.count(samplerName) > 0; }
bool Material::hasTexture(MaterialTextureSemantic semantic) const {
    return semanticTextures_.count(semantic) > 0;
}

const std::unordered_map<std::string, float>& Material::getFloatParams() const { return floats_; }
const std::unordered_map<std::string, TSVec3f>& Material::getVec3Params() const { return vec3s_; }
const std::unordered_map<std::string, TSVec4f>& Material::getVec4Params() const { return vec4s_; }
const std::unordered_map<std::string, TSMat4f>& Material::getMat4Params() const { return mat4s_; }
const std::unordered_map<std::string, Material::TextureBinding>& Material::getTextureBindings() const { return textures_; }
const std::unordered_map<MaterialTextureSemantic, Material::TextureBinding>&
Material::getSemanticTextureBindings() const {
    return semanticTextures_;
}

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

void Material::applyReflection(const Tasrovy::RHI::ShaderReflectionData& vertData, const Tasrovy::RHI::ShaderReflectionData& fragData) {
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

} // namespace Tasrovy::Render
