#pragma once

#include <cstdint>

namespace Tasrovy::RHI {

enum class GraphicsAPI : uint8_t {
    Unknown,
    Vulkan,
    D3D12
};

enum class Format : uint8_t {
    Unknown,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    BGRA8Srgb,
    RGBA16Float,
    RG16Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    Depth32Float,
    Depth32FloatStencil8
};

enum class ShaderStage : uint8_t {
    Vertex,
    Fragment,
    Compute
};

enum class ShaderStageFlags : uint8_t {
    None = 0,
    Vertex = 1u << 0u,
    Fragment = 1u << 1u,
    Compute = 1u << 2u,
    All = (1u << 0u) | (1u << 1u) | (1u << 2u)
};

constexpr ShaderStageFlags operator|(ShaderStageFlags lhs, ShaderStageFlags rhs) {
    return static_cast<ShaderStageFlags>(
        static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr bool hasFlag(ShaderStageFlags flags, ShaderStageFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

enum class BufferUsage : uint8_t {
    None = 0,
    Vertex = 1u << 0u,
    Index = 1u << 1u,
    TransferSource = 1u << 2u,
    Uniform = 1u << 3u,
    Storage = 1u << 4u,
    Indirect = 1u << 5u
};

constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) {
    return static_cast<BufferUsage>(
        static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr bool hasFlag(BufferUsage flags, BufferUsage flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

enum class PrimitiveTopology : uint8_t {
    PointList,
    LineList,
    TriangleList
};

enum class CullMode : uint8_t {
    None,
    Front,
    Back
};

enum class FrontFace : uint8_t {
    Clockwise,
    CounterClockwise
};

enum class CompareOp : uint8_t {
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual
};

enum class BlendMode : uint8_t {
    Off,
    Alpha,
    Additive
};

enum class ImageLayout : uint8_t {
    Undefined,
    ColorAttachment,
    DepthAttachment,
    DepthReadOnly,
    ShaderRead,
    General,
    Present
};

struct DescriptorBufferInfo {
    uint64_t backendBuffer = 0;
    uint64_t offset = 0;
    uint64_t range = 0;
};

struct DescriptorImageInfo {
    uint64_t backendSampler = 0;
    uint64_t backendView = 0;
    ImageLayout imageLayout = ImageLayout::Undefined;
};

} // namespace Tasrovy::RHI
