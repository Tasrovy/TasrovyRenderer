#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <optional>
#include <vector>
#include "RHITypes.h"

namespace Tasrovy::Render {
struct FramePacket;
}

namespace Tasrovy::RHI {

enum class FrameTextureFormat {
    RGBA8Unorm,
    RGBA16Float,
    RG16Float,
    Depth32Float,
    Swapchain
};

enum class FrameTextureExtent {
    InternalRelative,
    DisplayRelative,
    Fixed
};

struct FrameTextureDescription {
    std::string name;
    FrameTextureFormat format = FrameTextureFormat::RGBA16Float;
    FrameTextureExtent extent = FrameTextureExtent::InternalRelative;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    bool external = false;
};

enum class RenderResourceState {
    Undefined,
    ShaderRead,
    ColorAttachment,
    DepthReadOnly,
    DepthAttachment,
    StorageRead,
    StorageWrite,
    HostWrite,
    TransferRead,
    TransferWrite,
    Present
};

enum class RenderResourceKind : uint8_t {
    Texture,
    Buffer
};

struct RenderResourceTransition {
    uint64_t resourceId = 0;
    std::string resourceName;
    RenderResourceState before = RenderResourceState::Undefined;
    RenderResourceState after = RenderResourceState::Undefined;
    bool previousFrame = false;
    bool forceMemoryBarrier = false;
    RenderResourceKind kind = RenderResourceKind::Texture;
};

struct RenderResourceLifetimePlan {
    uint64_t resourceId = 0;
    std::string resourceName;
    size_t firstPass = 0;
    size_t lastPass = 0;
    bool external = false;
    bool persistent = false;
    bool storage = false;
    int32_t allocationSlot = -1;
    FrameTextureDescription description;
};

struct FrameBufferDescription {
    std::string name;
    uint64_t byteSize = 0;
    uint32_t usageFlags = 0;
    bool hostVisible = false;
    bool external = false;
};

struct RenderBufferLifetimePlan {
    uint64_t resourceId = 0;
    std::string resourceName;
    size_t firstPass = 0;
    size_t lastPass = 0;
    bool external = false;
    FrameBufferDescription description;
};

struct RHIVertexAttributePlan {
    uint32_t location = 0;
    Format format = Format::Unknown;
    uint32_t offset = 0;
};

struct RHIVertexLayoutPlan {
    uint32_t stride = 0;
    std::vector<RHIVertexAttributePlan> attributes;
};

enum class RHIDescriptorTypePlan : uint8_t {
    UniformBuffer,
    CombinedImageSampler,
    StorageImage,
    StorageBuffer
};

struct RHIDescriptorBindingPlan {
    uint32_t binding = 0;
    RHIDescriptorTypePlan type = RHIDescriptorTypePlan::UniformBuffer;
    ShaderStageFlags stages = ShaderStageFlags::None;
    uint64_t resourceId = 0;
    bool previousFrame = false;
};

struct RHIDescriptorLayoutPlan {
    std::vector<RHIDescriptorBindingPlan> bindings;
};

struct RHIDescriptorPoolSizePlan {
    RHIDescriptorTypePlan type = RHIDescriptorTypePlan::UniformBuffer;
    uint32_t descriptorsPerSet = 0;
};

struct RHIDescriptorPoolPlan {
    std::vector<RHIDescriptorPoolSizePlan> sizes;
};

struct RHIDescriptorSetPlan {
    uint32_t setsPerFrame = 1;
    uint32_t uniformByteSize = 0;
};

struct RHIPipelinePermutationPlan {
    uint64_t key = 0;
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    std::string computeShaderSource;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
    std::string computeEntryPoint;
};

struct RHIPipelinePlan {
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    std::string computeShaderSource;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
    std::string computeEntryPoint;
    RHIVertexLayoutPlan vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::Back;
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::Less;
    BlendMode blendMode = BlendMode::Off;
    RHIDescriptorLayoutPlan descriptorLayout;
    RHIDescriptorPoolPlan descriptorPool;
    RHIDescriptorSetPlan descriptorSets;
    std::vector<RHIPipelinePermutationPlan> permutations;
};

struct RHIAttachmentPlan {
    uint64_t resourceId = 0;
    std::string resourceName;
    uint32_t load = 0;
    uint32_t store = 0;
    bool readOnly = false;
    float clearDepth = 1.0f;
};

struct RenderPassExecutionPlan {
    uint64_t passId = 0;
    size_t packetPassIndex = 0;
    std::string name;
    uint32_t execution = 0;
    uint32_t passType = 0;
    std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    RHIPipelinePlan pipeline;
    std::vector<RHIAttachmentPlan> colorAttachments;
    std::optional<RHIAttachmentPlan> depthAttachment;
    uint32_t drawCount = 0;
    std::vector<RenderResourceTransition> preTransitions;
    std::vector<RenderResourceTransition> postTransitions;
};

struct RenderFrameExecutionPlan {
    uint64_t frameNumber = 0;
    std::vector<RenderResourceLifetimePlan> resources;
    std::vector<RenderBufferLifetimePlan> buffers;
    std::vector<RenderPassExecutionPlan> passes;
    std::vector<std::string> diagnostics;

    bool valid() const { return diagnostics.empty(); }
};

class RHIFrameCompiler {
public:
    RenderFrameExecutionPlan compile(
        const Tasrovy::Render::FramePacket& packet) const;
};

} // namespace Tasrovy::RHI
