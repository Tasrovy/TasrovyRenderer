#pragma once

#include "../RenderFramePlan.h"

#include <volk.h>

namespace Tasrovy::RHI::Vulkan {

struct VulkanBarrierState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags access = 0;
};

VulkanBarrierState translateResourceState(
    RenderResourceState state);

} // namespace Tasrovy::RHI::Vulkan
