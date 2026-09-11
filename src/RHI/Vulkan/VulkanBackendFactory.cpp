#include "VulkanDeviceBackend.h"
#include "VulkanFrameExecutor.h"
#ifdef TASROVY_ENABLE_DLSS_NR
#include "VulkanDlssNrExecutor.h"
#endif

#include "../DeviceBackend.h"
#include "../Device.h"
#include "../ExternalFeatureExecutor.h"
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

std::unique_ptr<IExternalFeatureExecutor> createExternalFeatureExecutor(
    Device& device) {
#if defined(TASROVY_ENABLE_DLSS_NR) && \
    TASROVY_ALLOW_UNTRUSTED_DLSS_NR_RUNTIME
    return std::make_unique<Vulkan::VulkanDlssNrExecutor>(
        device.getBackendInteropContext());
#else
    (void)device;
    return {};
#endif
}

} // namespace Tasrovy::RHI
