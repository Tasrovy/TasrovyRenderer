#pragma once

#include "../RHITypes.h"

#include <volk.h>

namespace Tasrovy::RHI::Vulkan {

VkFormat toVkFormat(Format format);
Format fromVkFormat(VkFormat format);
VkShaderStageFlagBits toVkShaderStage(ShaderStage stage);
VkShaderStageFlags toVkShaderStages(ShaderStageFlags stages);
VkBufferUsageFlags toVkBufferUsage(BufferUsage usage);
VkPrimitiveTopology toVkTopology(PrimitiveTopology topology);
VkCullModeFlags toVkCullMode(CullMode mode);
VkFrontFace toVkFrontFace(FrontFace face);
VkCompareOp toVkCompareOp(CompareOp op);
VkImageLayout toVkImageLayout(ImageLayout layout);
ImageLayout fromVkImageLayout(VkImageLayout layout);

} // namespace Tasrovy::RHI::Vulkan
