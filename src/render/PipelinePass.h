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

enum class PipelinePassExecution {
    Mesh,
    Fullscreen,
    Skybox,
    Compute,
    UI
};

enum class PipelineVertexFormat : uint8_t {
    Float2,
    Float3,
    Float4
};

struct PipelineVertexAttribute {
    uint32_t location = 0;
    PipelineVertexFormat format = PipelineVertexFormat::Float3;
    uint32_t offset = 0;
};

struct PipelineVertexLayout {
    uint32_t stride = 0;
    std::vector<PipelineVertexAttribute> attributes;
};

namespace ImportedResourceHandles {
inline constexpr const char* Skybox = "scene.skybox";
inline constexpr const char* IblIrradiance = "scene.ibl.irradiance";
inline constexpr const char* IblPrefiltered = "scene.ibl.prefiltered";
inline constexpr const char* IblBrdfLut = "scene.ibl.brdf_lut";
}

using ImportedResourceHandle = std::string;

struct PipelineImportedTextureBinding {
    uint32_t binding = 0;
    ImportedResourceHandle handle;
    uint32_t shaderStages = 0;
};

inline constexpr uint32_t PipelineShaderStageVertex = 1u << 0u;
inline constexpr uint32_t PipelineShaderStageFragment = 1u << 1u;
inline constexpr uint32_t PipelineShaderStageCompute = 1u << 2u;

// Describes how the engine's standard frame parameters are populated. The
// pass name remains diagnostic-only and never participates in execution.
namespace ParameterProviders {
inline constexpr const char* Standard = "builtin.standard";
inline constexpr const char* Shadow = "builtin.shadow";
inline constexpr const char* Lighting = "builtin.lighting";
inline constexpr const char* Skybox = "builtin.skybox";
inline constexpr const char* TemporalAA = "builtin.temporal_aa";
inline constexpr const char* TemporalUpscale = "builtin.temporal_upscale";
inline constexpr const char* OutlineTemporal = "builtin.outline_temporal";
inline constexpr const char* BloomPrefilter = "builtin.bloom_prefilter";
inline constexpr const char* SSAO = "builtin.ssao";
inline constexpr const char* DepthOfField = "builtin.depth_of_field";
inline constexpr const char* MotionBlur = "builtin.motion_blur";
inline constexpr const char* FinalComposite = "builtin.final_composite";
}

struct VirtualShadowPage {
    uint32_t pageX = 0;
    uint32_t pageY = 0;
    uint32_t pageSize = 0;
    uint32_t atlasSize = 0;
    uint32_t virtualLevel = 0;
};

struct PipelineShaderPermutation {
    uint64_t key = 0;
    std::shared_ptr<Shader> vertexShader;
    std::shared_ptr<Shader> fragmentShader;
    std::shared_ptr<Shader> computeShader;
};

struct PipelineDispatchCommand {
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;
};

struct PipelineCopyCommand {
    std::string source;
    std::string destination;
    uint64_t sourceOffset = 0;
    uint64_t destinationOffset = 0;
    uint64_t byteSize = 0;
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
    void setExecution(PipelinePassExecution execution);
    PipelinePassExecution getExecution() const;
    void setParameterProvider(std::string providerId);
    const std::string& getParameterProvider() const;
    void setViewIndex(uint32_t viewIndex);
    uint32_t getViewIndex() const;
    void setVirtualShadowPage(const VirtualShadowPage& page);
    const VirtualShadowPage* getVirtualShadowPage() const;

    void setState(const PassState& state);
    const PassState& getState() const;

    // --- Shaders ---
    void setVertexShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getVertexShader() const;
    void setFragmentShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getFragmentShader() const;
    void setComputeShader(std::shared_ptr<Shader> shader);
    std::shared_ptr<Shader> getComputeShader() const;
    void addShaderPermutation(PipelineShaderPermutation permutation);
    const std::vector<PipelineShaderPermutation>& getShaderPermutations() const;
    void setSelectedPermutationKey(uint64_t key);
    uint64_t getSelectedPermutationKey() const;
    void setVertexLayout(PipelineVertexLayout layout);
    const PipelineVertexLayout& getVertexLayout() const;
    void setUniformByteSize(uint32_t byteSize, uint32_t shaderStages);
    uint32_t getUniformByteSize() const;
    uint32_t getUniformShaderStages() const;
    void addImportedTexture(PipelineImportedTextureBinding binding);
    const std::vector<PipelineImportedTextureBinding>&
        getImportedTextures() const;
    void setDispatch(uint32_t groupCountX, uint32_t groupCountY = 1,
        uint32_t groupCountZ = 1);
    const PipelineDispatchCommand* getDispatch() const;
    void addCopyCommand(PipelineCopyCommand command);
    const std::vector<PipelineCopyCommand>& getCopyCommands() const;

    // --- Scene objects ---
    void addObject(std::weak_ptr<Object> object);
    void removeObject(const Object* object);
    void clearObjects();
    const std::vector<std::weak_ptr<Object>>& getObjects() const;

    // --- Logical texture resources ---
    // A current-frame texture cannot be sampled/storage-read and written by
    // the same pass. Reading a previous-frame version remains valid.
    void addSampledTexture(
        std::string slot,
        std::string resource,
        uint32_t binding = 0,
        bool previousFrame = false,
        std::string producerPass = {});
    void addStorageTexture(
        std::string slot,
        std::string resource,
        uint32_t binding,
        PipelineResourceAccess access,
        bool previousFrame = false);
    void addStorageBuffer(
        std::string slot,
        std::string resource,
        uint32_t binding,
        PipelineResourceAccess access);
    void addColorAttachment(
        std::string resource,
        AttachmentLoad load = AttachmentLoad::Clear,
        AttachmentStore store = AttachmentStore::Store,
        std::string producerPass = {});
    void setDepthAttachment(
        std::string resource,
        AttachmentLoad load = AttachmentLoad::Clear,
        AttachmentStore store = AttachmentStore::Store,
        bool readOnly = false,
        float clearDepth = 1.0f,
        std::string producerPass = {});
    void addExecutionDependency(std::string producerPass);
    const std::vector<std::string>& getExecutionDependencies() const;
    const std::vector<SampledTextureInput>& getSampledTextures() const;
    const std::vector<ColorAttachmentRef>& getColorAttachments() const;
    const DepthAttachmentRef* getDepthAttachment() const;
    std::vector<PipelineResourceRef> getReadResources() const;
    std::vector<PipelineResourceRef> getWriteResources() const;

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
    PipelinePassExecution execution_ = PipelinePassExecution::Mesh;
    std::string parameterProvider_ = ParameterProviders::Standard;
    uint32_t viewIndex_ = 0;
    std::unique_ptr<VirtualShadowPage> virtualShadowPage_;
    PassState state_;
    std::vector<std::weak_ptr<Object>> objects_;
    std::vector<SampledTextureInput> sampledTextures_;
    std::vector<ColorAttachmentRef> colorAttachments_;
    std::unique_ptr<DepthAttachmentRef> depthAttachment_;
    std::vector<MaterialTextureRequirement> materialTextures_;
    std::shared_ptr<Shader> vertexShader_;
    std::shared_ptr<Shader> fragmentShader_;
    std::shared_ptr<Shader> computeShader_;
    std::vector<PipelineShaderPermutation> shaderPermutations_;
    uint64_t selectedPermutationKey_ = 0;
    PipelineVertexLayout vertexLayout_;
    uint32_t uniformByteSize_ = 0;
    uint32_t uniformShaderStages_ = 0;
    std::vector<PipelineImportedTextureBinding> importedTextures_;
    std::unique_ptr<PipelineDispatchCommand> dispatch_;
    std::vector<PipelineCopyCommand> copyCommands_;
    std::vector<PipelineResourceRef> storageTextures_;
    std::vector<PipelineResourceRef> storageBuffers_;
    std::vector<std::string> executionDependencies_;
};

} // namespace Tasrovy::Render
