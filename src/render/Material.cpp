#include "Material.h"
#include "MaterialDescriptor.h"
#include "Shader.h"

#include <stdexcept>

namespace Tasrovy::Render {

Material::Material() = default;

Material::Material(std::shared_ptr<const MaterialDescriptor> descriptor)
    : descriptor_(std::move(descriptor)) {
    if (!descriptor_) {
        return;
    }
    floats_ = descriptor_->getFloatParams();
    vec3s_ = descriptor_->getVec3Params();
    vec4s_ = descriptor_->getVec4Params();
    castShadows_ = descriptor_->castsShadows();
    alphaCutoff_ = descriptor_->getAlphaCutoff();
    surface_ = static_cast<MaterialSurface>(descriptor_->getSurface());
    for (const auto& property : descriptor_->getProperties()) {
        if (property.type != MaterialPropertyType::Texture2D) {
            continue;
        }
        const auto path = descriptor_->getTexturePaths().find(property.name);
        const auto sampling =
            descriptor_->getTextureSampling().find(property.name);
        textures_.emplace(
            property.name,
            TextureBinding{
                path == descriptor_->getTexturePaths().end()
                    ? std::string()
                    : path->second,
                sampling == descriptor_->getTextureSampling().end()
                    ? MaterialTextureUvSampling{}
                    : sampling->second
            });
    }
}

std::shared_ptr<Material> Material::create() {
    return std::shared_ptr<Material>(new Material());
}

std::shared_ptr<Material> Material::create(
    std::shared_ptr<const MaterialDescriptor> descriptor) {
    return std::shared_ptr<Material>(new Material(std::move(descriptor)));
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
}

std::shared_ptr<Shader> Material::getShader() const {
    if (auto fragmentShader = fragmentShader_.lock()) {
        return fragmentShader;
    }
    return vertexShader_.lock();
}

void Material::setVertexShader(std::weak_ptr<Shader> shader) {
    vertexShader_ = shader;
}

void Material::setFragmentShader(std::weak_ptr<Shader> shader) {
    fragmentShader_ = shader;
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
    if (descriptor_) {
        const auto& property = descriptor_->requireProperty(samplerName);
        if (property.type != MaterialPropertyType::Texture2D) {
            throw std::invalid_argument(
                "material property is not a texture2D: " + samplerName);
        }
    }
    auto found = textures_.find(samplerName);
    if (found != textures_.end()) {
        found->second.path = texturePath;
        return;
    }
    if (descriptor_) {
        throw std::invalid_argument(
            "texture slot is not declared by material descriptor: " + samplerName);
    }
    textures_[samplerName] = {texturePath};
}

void Material::setTextureUvSampling(
    const std::string& samplerName,
    MaterialTextureUvSampling sampling) {
    auto found = textures_.find(samplerName);
    if (found == textures_.end()) {
        if (descriptor_) {
            const auto& property = descriptor_->requireProperty(samplerName);
            if (property.type != MaterialPropertyType::Texture2D) {
                throw std::invalid_argument(
                    "material property is not a texture2D: " + samplerName);
            }
        }
        found = textures_.emplace(
            samplerName, TextureBinding{}).first;
    }
    found->second.uvSampling = sampling;
}

void Material::clearTexture(const std::string& samplerName) {
    if (auto found = textures_.find(samplerName); found != textures_.end()) {
        found->second.path.clear();
    }
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

const Material::TextureBinding* Material::getTextureBinding(
    const std::string& samplerName) const {
    const auto it = textures_.find(samplerName);
    return it != textures_.end() ? &it->second : nullptr;
}

const Material::TextureBinding* Material::resolveTexture(
    const MaterialTextureRequirement& requirement) const {
    return getTextureBinding(requirement.slot);
}

bool Material::hasFloat(const std::string& name) const { return floats_.count(name) > 0; }
bool Material::hasVec3(const std::string& name) const { return vec3s_.count(name) > 0; }
bool Material::hasTexture(const std::string& samplerName) const {
    const auto found = textures_.find(samplerName);
    return found != textures_.end() && !found->second.path.empty();
}

std::shared_ptr<const MaterialDescriptor> Material::getDescriptor() const {
    return descriptor_;
}

const std::unordered_map<std::string, float>& Material::getFloatParams() const { return floats_; }
const std::unordered_map<std::string, TSVec3f>& Material::getVec3Params() const { return vec3s_; }
const std::unordered_map<std::string, TSVec4f>& Material::getVec4Params() const { return vec4s_; }
const std::unordered_map<std::string, TSMat4f>& Material::getMat4Params() const { return mat4s_; }
const std::unordered_map<std::string, Material::TextureBinding>& Material::getTextureBindings() const { return textures_; }

} // namespace Tasrovy::Render
