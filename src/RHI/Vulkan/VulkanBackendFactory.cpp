#include "VulkanDeviceBackend.h"
#include "VulkanFrameExecutor.h"

#include "../DeviceBackend.h"
#include "../FrameExecutorBackend.h"

#include <memory>

namespace Tasrovy::RHI {

std::unique_ptr<IDeviceBackend> createSelectedDeviceBackend(
    const SurfaceDeviceCreateInfo& createInfo) {
    return std::make_unique<Vulkan::VulkanDeviceBackend>(createInfo);
}

std::unique_ptr<IFrameExecutorBackend> createSelectedFrameExecutorBackend() {
    return std::make_unique<Vulkan::VulkanFrameExecutor>();
}

} // namespace Tasrovy::RHI
