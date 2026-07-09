#pragma once

#include <string>
#include <vector>
#include <memory>
#include "MaterialTexture.h"
#include "PipelineResource.h"
#include "TSVector.h"

namespace Tasrovy::Render {

class Shader;
class Object;

enum class BlendMode { Off, Alpha, Additive };
enum class CullMode { None, Front, Back };
enum class Topology { TriangleList, LineList, PointList };
enum class DepthTestMode { Less, LessOrEqual, Equal, Greater, NotEqual };
enum class PipelinePassType {
    Generic,
    Shadow,
    Geometry,
    Lighting,
    Skybox,
    Transparent,
    PostProcess
};

struct VertexInputPass {
    uint32_t vertexNumber = 0;
    uint32_t vertexSize = 0;
    uint16_t vertexType = 0;
    Topology topology = Topology::TriangleList;
};

struct VertexPass {
    std::weak_ptr<Shader> vertexShader;
};

struct RasterizationPass {
    CullMode cullMode = CullMode::Back;
};

struct FragmentPass {
    std::weak_ptr<Shader> fragmentShader;
};

struct DepthPass {
    bool depthTest = true;
    bool depthWrite = true;
    DepthTestMode testMode = DepthTestMode::Less;
};

struct ColorBlendingPass {
    BlendMode blendMode = BlendMode::Off;
    TSVec4f clearColor = TSVec4f(0.0f, 0.0f, 0.0f, 1.0f);
};

struct PassState {
    VertexInputPass vertexInput;
    VertexPass vertexPass;
    RasterizationPass rasterizationPass;
    FragmentPass fragmentPass;
    DepthPass depthPass;
    ColorBlendingPass colorBlendingPass;
};

class PipelinePass : public std::enable_shared_from_this<PipelinePass> {
public:
    static std::shared_ptr<PipelinePass> create(const std::string& name = "");

    void setName(const std::string& name);
    const std::string& getName() const;
    void setType(PipelinePassType type);
    PipelinePassType getType() const;

    void setState(const PassState& state);
    const PassState& getState() const;

    // --- Shaders ---
    void setVertexShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getVertexShader() const;
    void setFragmentShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getFragmentShader() const;

    // --- Scene objects ---
    void addObject(std::weak_ptr<Object> object);
    void removeObject(const Object* object);
    void clearObjects();
    const std::vector<std::weak_ptr<Object>>& getObjects() const;

    // --- Logical texture resources ---
    void addSampledTexture(std::string slot, std::string resource);
    void addColorAttachment(
        std::string resource,
        AttachmentLoad load = AttachmentLoad::Clear,
        AttachmentStore store = AttachmentStore::Store);
    void setDepthAttachment(
        std::string resource,
        AttachmentLoad load = AttachmentLoad::Clear,
        AttachmentStore store = AttachmentStore::Store,
        bool readOnly = false,
        float clearDepth = 1.0f);
    const std::vector<SampledTextureInput>& getSampledTextures() const;
    const std::vector<ColorAttachmentRef>& getColorAttachments() const;
    const DepthAttachmentRef* getDepthAttachment() const;

    // --- Per-material texture inputs ---
    void addMaterialTexture(MaterialTextureRequirement requirement);
    const std::vector<MaterialTextureRequirement>& getMaterialTextures() const;

    // --- Vertex input ---
    void setTopology(Topology t);
    Topology getTopology() const;

    // --- Rasterization ---
    void setCullMode(CullMode c);
    CullMode getCullMode() const;

    // --- Depth ---
    void setDepthTest(bool enable);
    void setDepthWrite(bool enable);
    void setDepthTestMode(DepthTestMode mode);
    bool getDepthTest() const;
    bool getDepthWrite() const;
    DepthTestMode getDepthTestMode() const;

    // --- Color blending ---
    void setBlendMode(BlendMode b);
    void setClearColor(const TSVec4f& color);
    BlendMode getBlendMode() const;
    const TSVec4f& getClearColor() const;

private:
    PipelinePass() = default;
    explicit PipelinePass(const std::string& name);

    std::string name_;
    PipelinePassType type_ = PipelinePassType::Generic;
    PassState state_;
    std::vector<std::weak_ptr<Object>> objects_;
    std::vector<SampledTextureInput> sampledTextures_;
    std::vector<ColorAttachmentRef> colorAttachments_;
    std::unique_ptr<DepthAttachmentRef> depthAttachment_;
    std::vector<MaterialTextureRequirement> materialTextures_;
    std::shared_ptr<Shader> vertexShader_;
    std::shared_ptr<Shader> fragmentShader_;
};

} // namespace Tasrovy::Render
