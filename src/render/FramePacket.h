#pragma once

#include "PipelinePass.h"
#include "PipelineResource.h"
#include "Shader.h"
#include "TSMatrix.h"
#include "TSVector.h"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Tasrovy::Render {

using RenderResourceId = uint64_t;
using RenderObjectId = uint64_t;
using RenderMeshId = uint64_t;
using RenderMaterialId = uint64_t;

inline constexpr uint32_t FrameViewUniformBinding = 20;
inline constexpr uint32_t FrameObjectDataBinding = 21;
inline constexpr uint32_t FrameMaterialDataBinding = 22;
inline constexpr uint32_t FrameSceneLightBinding = 23;

struct FrameShaderPacket {
    std::string assetPath;
    std::string entryPoint;
    ShaderType stage = ShaderType::Vertex;

    bool empty() const { return assetPath.empty(); }
};

struct FrameTexturePacket {
    RenderResourceId id = 0;
    PipelineTextureDesc description;
};

struct FrameBufferPacket {
    RenderResourceId id = 0;
    PipelineBufferDesc description;
};

struct FrameResourceUse {
    RenderResourceId id = 0;
    std::string slot;
    std::string resourceName;
    uint32_t binding = 0;
    PipelineResourceAccess access = PipelineResourceAccess::SampledRead;
    bool previousFrame = false;
};

enum class FrameVertexFormat : uint8_t {
    Float2,
    Float3,
    Float4
};

struct FrameVertexAttribute {
    uint32_t location = 0;
    FrameVertexFormat format = FrameVertexFormat::Float3;
    uint32_t offset = 0;
};

struct FrameVertexLayout {
    uint32_t stride = 0;
    std::vector<FrameVertexAttribute> attributes;
};

enum class FrameDescriptorType : uint8_t {
    UniformBuffer,
    CombinedImageSampler,
    StorageImage,
    StorageBuffer
};

inline constexpr uint32_t FrameShaderStageVertex = 1u << 0u;
inline constexpr uint32_t FrameShaderStageFragment = 1u << 1u;
inline constexpr uint32_t FrameShaderStageCompute = 1u << 2u;

struct FrameDescriptorBinding {
    uint32_t binding = 0;
    FrameDescriptorType type = FrameDescriptorType::UniformBuffer;
    uint32_t stages = FrameShaderStageFragment;
    RenderResourceId resourceId = 0;
    bool previousFrame = false;
};

struct FrameDescriptorLayout {
    std::vector<FrameDescriptorBinding> bindings;
    uint32_t setsPerFrame = 1;
};

struct FramePassParameters {
    uint32_t uniformByteSize = 0;
    std::vector<std::byte> uniformData;
    std::vector<FrameDescriptorBinding> resourceBindings;
};

enum class FrameDescriptorSource : uint8_t {
    UniformData,
    RenderTexture,
    MaterialTexture,
    ImportedResource,
    RenderBuffer,
    ViewUniform,
    ObjectData,
    MaterialData,
    SceneLights
};

struct FrameDescriptorWrite {
    uint32_t binding = 0;
    FrameDescriptorType type = FrameDescriptorType::UniformBuffer;
    FrameDescriptorSource source = FrameDescriptorSource::UniformData;
    RenderResourceId resourceId = 0;
    RenderMaterialId materialId = 0;
    std::string resourceName;
    std::string materialSlot;
    std::string importedResource;
    bool previousFrame = false;
};

struct FramePipelinePermutation {
    uint64_t key = 0;
    FrameShaderPacket vertexShader;
    FrameShaderPacket fragmentShader;
    FrameShaderPacket computeShader;
};

struct FrameAttachmentPacket {
    RenderResourceId resourceId = 0;
    std::string resourceName;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    bool readOnly = false;
    float clearDepth = 1.0f;
};

struct FrameMeshResource {
    RenderMeshId id = 0;
};

struct FrameMaterialResource {
    RenderMaterialId id = 0;
    uint32_t materialIndex = 0;
};

struct FrameDrawPacket {
    RenderMeshId meshId = 0;
    uint32_t objectIndex = 0;
    uint32_t materialIndex = 0;
    uint32_t submeshIndex = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    bool flipProjectionY = true;
    std::vector<FrameDescriptorWrite> descriptorWrites;
};

enum class FrameCommandType : uint8_t {
    Draw,
    DrawIndexed,
    DrawSkybox,
    Dispatch,
    CopyBuffer
};

struct FrameCommandPacket {
    FrameCommandType type = FrameCommandType::Draw;
    uint32_t drawIndex = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;
    RenderResourceId sourceResourceId = 0;
    RenderResourceId destinationResourceId = 0;
    uint64_t sourceOffset = 0;
    uint64_t destinationOffset = 0;
    uint64_t byteSize = 0;
};

struct FramePipelineState {
    Topology topology = Topology::TriangleList;
    CullMode cullMode = CullMode::Back;
    bool depthTest = true;
    bool depthWrite = true;
    DepthTestMode depthTestMode = DepthTestMode::Less;
    BlendMode blendMode = BlendMode::Off;
    TSVec4f clearColor = TSVec4f(0.0f, 0.0f, 0.0f, 1.0f);
};

struct FramePassPacket {
    uint64_t id = 0;
    std::string name;
    PipelinePassType type = PipelinePassType::Generic;
    PipelinePassExecution execution = PipelinePassExecution::Mesh;
    std::string parameterProvider = ParameterProviders::Standard;
    uint32_t viewIndex = 0;
    FramePipelineState state;
    FrameShaderPacket vertexShader;
    FrameShaderPacket fragmentShader;
    FrameShaderPacket computeShader;
    FrameVertexLayout vertexLayout;
    FrameDescriptorLayout descriptorLayout;
    FramePassParameters parameters;
    std::vector<FrameDescriptorWrite> descriptorWrites;
    uint64_t selectedPermutationKey = 0;
    std::vector<FramePipelinePermutation> permutations;
    std::vector<MaterialTextureRequirement> materialTextures;
    std::vector<RenderObjectId> objectIds;
    std::vector<SampledTextureInput> sampledTextures;
    std::vector<FrameResourceUse> reads;
    std::vector<FrameResourceUse> writes;
    std::vector<FrameAttachmentPacket> colorAttachments;
    std::optional<FrameAttachmentPacket> depthAttachment;
    std::optional<VirtualShadowPage> virtualShadowPage;
    std::vector<FrameDrawPacket> draws;
    std::vector<FrameCommandPacket> commands;

    FramePassPacket() = default;
    FramePassPacket(FramePassPacket&&) noexcept = default;
    FramePassPacket& operator=(FramePassPacket&&) noexcept = default;
    FramePassPacket(const FramePassPacket&) = default;
    FramePassPacket& operator=(const FramePassPacket&) = default;

    const std::string& getName() const { return name; }
    PipelinePassType getType() const { return type; }
    PipelinePassExecution getExecution() const { return execution; }
    Topology getTopology() const { return state.topology; }
    CullMode getCullMode() const { return state.cullMode; }
    bool getDepthTest() const { return state.depthTest; }
    bool getDepthWrite() const { return state.depthWrite; }
    DepthTestMode getDepthTestMode() const { return state.depthTestMode; }
    BlendMode getBlendMode() const { return state.blendMode; }
    const TSVec4f& getClearColor() const { return state.clearColor; }
    const std::vector<RenderObjectId>& getObjectIds() const {
        return objectIds;
    }
    const std::vector<MaterialTextureRequirement>& getMaterialTextures() const {
        return materialTextures;
    }
    const std::vector<SampledTextureInput>& getSampledTextures() const {
        return sampledTextures;
    }
    const std::vector<FrameAttachmentPacket>& getColorAttachments() const {
        return colorAttachments;
    }
    const FrameAttachmentPacket* getDepthAttachment() const {
        return depthAttachment ? &*depthAttachment : nullptr;
    }
    const VirtualShadowPage* getVirtualShadowPage() const {
        return virtualShadowPage ? &*virtualShadowPage : nullptr;
    }
};

struct FrameCameraPacket {
    bool valid = false;
    TSMat4f view = TSMat4f(1.0f);
    TSMat4f projection = TSMat4f(1.0f);
    TSVec3f position = TSVec3f(0.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct FramePacket {
    uint64_t frameNumber = 0;
    std::string pipelineName;
    FrameCameraPacket camera;
    std::vector<FrameTexturePacket> textures;
    std::vector<FrameBufferPacket> buffers;
    std::vector<FrameMeshResource> meshes;
    std::vector<FrameMaterialResource> materials;
    std::vector<FramePassPacket> passes;
    std::vector<std::string> diagnostics;

    bool valid() const { return diagnostics.empty(); }
};

} // namespace Tasrovy::Render
