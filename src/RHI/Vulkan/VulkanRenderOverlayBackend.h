#pragma once

#include <volk.h>
#include <cstdint>

namespace Tasrovy::RHI::Vulkan {

class VulkanRenderOverlayBackend {
public:
    virtual ~VulkanRenderOverlayBackend() = default;
    virtual void recordDrawData(
        VkCommandBuffer commandBuffer, uint64_t frameToken) = 0;
};

} // namespace Tasrovy::RHI::Vulkan
