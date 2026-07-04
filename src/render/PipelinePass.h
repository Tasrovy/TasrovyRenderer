#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Tasrovy {

class Shader;
class Object;

enum class BlendMode { Off, Alpha, Additive };
enum class CullMode { None, Front, Back };

struct PassState {
    bool depthTest = true;
    bool depthWrite = true;
    BlendMode blendMode = BlendMode::Off;
    CullMode cullMode = CullMode::Back;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool clearDepth = true;
};

struct TextureSlot {
    std::string name;
    std::string resourcePath;
};

class PipelinePass : public std::enable_shared_from_this<PipelinePass> {
public:
    static std::shared_ptr<PipelinePass> create(const std::string& name = "");

    void setName(const std::string& name);
    const std::string& getName() const;

    void setShader(std::weak_ptr<Shader> shader);
    std::shared_ptr<Shader> getShader() const;

    void setInputTexture(const std::string& name, const std::string& resourcePath);
    void setOutputTexture(const std::string& name, const std::string& resourcePath);
    const std::vector<TextureSlot>& getInputTextures() const;
    const std::vector<TextureSlot>& getOutputTextures() const;

    void addObject(std::weak_ptr<Object> object);
    void removeObject(Object* object);
    const std::vector<std::weak_ptr<Object>>& getObjects() const;

    void setState(const PassState& state);
    const PassState& getState() const;

private:
    PipelinePass() = default;
    explicit PipelinePass(const std::string& name);

    std::string name_;
    std::weak_ptr<Shader> shader_;
    std::vector<TextureSlot> inputTextures_;
    std::vector<TextureSlot> outputTextures_;
    std::vector<std::weak_ptr<Object>> objects_;
    PassState state_;
};

} // namespace Tasrovy
