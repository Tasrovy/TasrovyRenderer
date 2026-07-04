#pragma once

#include "TSVector.h"
#include "TSMatrix.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace Tasrovy {

class Shader;

class Material : public std::enable_shared_from_this<Material> {
public:
    static std::shared_ptr<Material> create();
    static std::shared_ptr<Material> create(std::weak_ptr<Shader> shader);

    void setShader(std::weak_ptr<Shader> shader);
    std::shared_ptr<Shader> getShader() const;

    // Generic parameter access
    void setFloat(const std::string& name, float value);
    void setVec3(const std::string& name, TSVec3f value);
    void setVec4(const std::string& name, TSVec4f value);
    void setMat4(const std::string& name, TSMat4f value);
    void setTexture(const std::string& samplerName, const std::string& texturePath);
    void setTexture(const std::string& samplerName, uint32_t binding, const std::string& texturePath);

    float getFloat(const std::string& name, float fallback = 0.0f) const;
    TSVec3f getVec3(const std::string& name, TSVec3f fallback = TSVec3f(0.0f)) const;
    TSVec4f getVec4(const std::string& name, TSVec4f fallback = TSVec4f(0.0f)) const;
    TSMat4f getMat4(const std::string& name) const;
    std::string getTexture(const std::string& samplerName) const;

    bool hasFloat(const std::string& name) const;
    bool hasVec3(const std::string& name) const;
    bool hasTexture(const std::string& samplerName) const;

    // Bulk access for RHI binding
    const std::unordered_map<std::string, float>& getFloatParams() const;
    const std::unordered_map<std::string, TSVec3f>& getVec3Params() const;
    const std::unordered_map<std::string, TSVec4f>& getVec4Params() const;
    const std::unordered_map<std::string, TSMat4f>& getMat4Params() const;

    struct TextureBinding {
        uint32_t binding;
        std::string path;
    };
    const std::unordered_map<std::string, TextureBinding>& getTextureBindings() const;

    // SPIR-V reflection data (populated by RHI layer)
    struct ReflectedUniform {
        std::string name;
        uint32_t offset;
        uint32_t size;
    };
    void setReflectedUniforms(std::vector<ReflectedUniform> uniforms);
    const std::vector<ReflectedUniform>& getReflectedUniforms() const;

private:
    Material();

    std::weak_ptr<Shader> shader_;

    std::unordered_map<std::string, float> floats_;
    std::unordered_map<std::string, TSVec3f> vec3s_;
    std::unordered_map<std::string, TSVec4f> vec4s_;
    std::unordered_map<std::string, TSMat4f> mat4s_;
    std::unordered_map<std::string, TextureBinding> textures_;
    std::vector<ReflectedUniform> reflectedUniforms_;
};

} // namespace Tasrovy
