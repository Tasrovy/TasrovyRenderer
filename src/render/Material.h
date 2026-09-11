#pragma once

#include "TSVector.h"
#include "TSMatrix.h"
#include "MaterialTexture.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace Tasrovy::Render {

class Shader;
class MaterialDescriptor;
class MaterialTechnique;

enum class MaterialSurface {
    Opaque,
    Masked,
    Transparent
};

class Material : public std::enable_shared_from_this<Material> {
public:
    struct TextureBinding {
        std::string path;
        MaterialTextureUvSampling uvSampling;
        bool generateMipmaps = true;
    };

    static std::shared_ptr<Material> create();
    static std::shared_ptr<Material> create(
        std::shared_ptr<const MaterialDescriptor> descriptor);
    static std::shared_ptr<Material> create(std::shared_ptr<Shader> shader);
    static std::shared_ptr<Material> create(
        std::shared_ptr<Shader> vertexShader,
        std::shared_ptr<Shader> fragmentShader);

    void setShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getShader() const;
    void setVertexShader(std::shared_ptr<Shader> shader);
    void setFragmentShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getVertexShader() const;
    std::shared_ptr<Shader> getFragmentShader() const;

    // Generic parameter access
    void setFloat(const std::string& name, float value);
    void setVec3(const std::string& name, TSVec3f value);
    void setVec4(const std::string& name, TSVec4f value);
    void setMat4(const std::string& name, TSMat4f value);
    void setTexture(const std::string& samplerName, const std::string& texturePath);
    void setTextureUvSampling(
        const std::string& samplerName,
        MaterialTextureUvSampling sampling);
    void setTextureMipmaps(
        const std::string& samplerName,
        bool generateMipmaps);
    void clearTexture(const std::string& samplerName);
    void setSurface(MaterialSurface surface);
    MaterialSurface getSurface() const;
    void setAlphaCutoff(float alphaCutoff);
    float getAlphaCutoff() const;
    void setCastShadows(bool castShadows);
    bool castsShadows() const;

    float getFloat(const std::string& name, float fallback = 0.0f) const;
    TSVec3f getVec3(const std::string& name, TSVec3f fallback = TSVec3f(0.0f)) const;
    TSVec4f getVec4(const std::string& name, TSVec4f fallback = TSVec4f(0.0f)) const;
    TSMat4f getMat4(const std::string& name) const;
    std::string getTexture(const std::string& samplerName) const;
    const TextureBinding* getTextureBinding(const std::string& samplerName) const;
    const TextureBinding* resolveTexture(
        const MaterialTextureRequirement& requirement) const;

    bool hasFloat(const std::string& name) const;
    bool hasVec3(const std::string& name) const;
    bool hasTexture(const std::string& samplerName) const;
    std::shared_ptr<const MaterialDescriptor> getDescriptor() const;
    void setTechnique(std::shared_ptr<const MaterialTechnique> technique);
    std::shared_ptr<const MaterialTechnique> getTechnique() const;

    // Bulk access for RHI binding
    const std::unordered_map<std::string, float>& getFloatParams() const;
    const std::unordered_map<std::string, TSVec3f>& getVec3Params() const;
    const std::unordered_map<std::string, TSVec4f>& getVec4Params() const;
    const std::unordered_map<std::string, TSMat4f>& getMat4Params() const;

    const std::unordered_map<std::string, TextureBinding>& getTextureBindings() const;

private:
    Material();
    explicit Material(std::shared_ptr<const MaterialDescriptor> descriptor);

    std::shared_ptr<Shader> vertexShader_;
    std::shared_ptr<Shader> fragmentShader_;

    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, TSVec3f> vec3s_;
    std::unordered_map<std::string, TSVec4f> vec4s_;
    std::unordered_map<std::string, TSMat4f> mat4s_;
    std::unordered_map<std::string, TextureBinding> textures_;
    std::shared_ptr<const MaterialDescriptor> descriptor_;
    std::shared_ptr<const MaterialTechnique> technique_;
    MaterialSurface surface_ = MaterialSurface::Opaque;
    float alphaCutoff_ = 0.5f;
    bool castShadows_ = true;
};

} // namespace Tasrovy::Render
