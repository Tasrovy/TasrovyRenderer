#include "VulkanConversions.h"

#include <stdexcept>

namespace Tasrovy::RHI::Vulkan {

VkFormat toVkFormat(Format format) {
    switch (format) {
    case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
    case Format::RG8Unorm: return VK_FORMAT_R8G8_UNORM;
    case Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R8Uint: return VK_FORMAT_R8_UINT;
    case Format::R16Uint: return VK_FORMAT_R16_UINT;
    case Format::R32Uint: return VK_FORMAT_R32_UINT;
    case Format::RG16Uint: return VK_FORMAT_R16G16_UINT;
    case Format::R16Float: return VK_FORMAT_R16_SFLOAT;
    case Format::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
    case Format::RG32Float: return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGB32Float: return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R11G11B10Float: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::RGB10A2Unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::Depth16Unorm: return VK_FORMAT_D16_UNORM;
    case Format::Depth24UnormStencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case Format::Depth32FloatStencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::Unknown: break;
    }
    return VK_FORMAT_UNDEFINED;
}

Format fromVkFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: return Format::R8Unorm;
    case VK_FORMAT_R8G8_UNORM: return Format::RG8Unorm;
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8Srgb;
    case VK_FORMAT_R8_UINT: return Format::R8Uint;
    case VK_FORMAT_R16_UINT: return Format::R16Uint;
    case VK_FORMAT_R32_UINT: return Format::R32Uint;
    case VK_FORMAT_R16G16_UINT: return Format::RG16Uint;
    case VK_FORMAT_R16_SFLOAT: return Format::R16Float;
    case VK_FORMAT_R16G16_SFLOAT: return Format::RG16Float;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16Float;
    case VK_FORMAT_R32_SFLOAT: return Format::R32Float;
    case VK_FORMAT_R32G32_SFLOAT: return Format::RG32Float;
    case VK_FORMAT_R32G32B32_SFLOAT: return Format::RGB32Float;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32Float;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return Format::R11G11B10Float;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return Format::RGB10A2Unorm;
    case VK_FORMAT_D16_UNORM: return Format::Depth16Unorm;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return Format::Depth24UnormStencil8;
    case VK_FORMAT_D32_SFLOAT: return Format::Depth32Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return Format::Depth32FloatStencil8;
    default: return Format::Unknown;
    }
}

VkShaderStageFlagBits toVkShaderStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return VK_SHADER_STAGE_ALL;
}

VkShaderStageFlags toVkShaderStages(ShaderStageFlags stages) {
    VkShaderStageFlags result = 0;
    if (hasFlag(stages, ShaderStageFlags::Vertex)) result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(stages, ShaderStageFlags::Fragment)) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasFlag(stages, ShaderStageFlags::Compute)) result |= VK_SHADER_STAGE_COMPUTE_BIT;
    return result;
}

VkBufferUsageFlags toVkBufferUsage(BufferUsage usage) {
    VkBufferUsageFlags result = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(usage, BufferUsage::Vertex)) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Index)) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::TransferSource)) result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, BufferUsage::Uniform)) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Storage)) result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Indirect)) result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return result;
}

VkPrimitiveTopology toVkTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVkCullMode(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_BACK_BIT;
}

VkFrontFace toVkFrontFace(FrontFace face) {
    return face == FrontFace::CounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE
        : VK_FRONT_FACE_CLOCKWISE;
}

VkCompareOp toVkCompareOp(CompareOp op) {
    switch (op) {
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    }
    return VK_COMPARE_OP_LESS;
}

VkImageLayout toVkImageLayout(ImageLayout layout) {
    switch (layout) {
    case ImageLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
    case ImageLayout::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ImageLayout::DepthAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ImageLayout::DepthReadOnly: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ImageLayout::ShaderRead: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ImageLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
    case ImageLayout::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

ImageLayout fromVkImageLayout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return ImageLayout::ColorAttachment;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return ImageLayout::DepthAttachment;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return ImageLayout::DepthReadOnly;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return ImageLayout::ShaderRead;
    case VK_IMAGE_LAYOUT_GENERAL: return ImageLayout::General;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return ImageLayout::Present;
    default: return ImageLayout::Undefined;
    }
}

} // namespace Tasrovy::RHI::Vulkan
