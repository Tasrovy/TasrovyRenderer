#include "SceneRendererComponents.h"

#include "ResourceMonitor.h"
#include "../RHI/Device.h"
#include "../RHI/FrameScheduler.h"
#include "../ui/UI.h"
#include "../window/Window.h"

#include <algorithm>
#include <cmath>

namespace Tasrovy::Renderer {

namespace {

uint32_t makeEvenExtent(float value) {
    const uint32_t rounded =
        std::max(2u, static_cast<uint32_t>(std::lround(value)));
    return (rounded + 1u) & ~1u;
}

} // namespace

SceneRendererComponents::SceneRendererComponents(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFrames)
    : rhi(window, maxFrames),
      maxFramesInFlight(maxFrames) {
    auto& device = *rhi.device;
    const float resolutionScale =
        std::max(settings.internalResolutionPercent, 1.0f) * 0.01f;
    internalRenderWidth = makeEvenExtent(
        static_cast<float>(
            std::max(device.getFrameScheduler().getWidth(), 1u)) *
        resolutionScale);
    internalRenderHeight = makeEvenExtent(
        static_cast<float>(
            std::max(device.getFrameScheduler().getHeight(), 1u)) *
        resolutionScale);

    const auto overlayContext = device.getNativeOverlayContext();
    Tasrovy::UI::UIOverlay::CreateInfo overlayInfo{};
    overlayInfo.window = window.getHandle();
    overlayInfo.instance = overlayContext.instance;
    overlayInfo.physicalDevice = overlayContext.physicalDevice;
    overlayInfo.device = overlayContext.device;
    overlayInfo.graphicsQueue = overlayContext.graphicsQueue;
    overlayInfo.queueFamily = overlayContext.queueFamily;
    overlayInfo.minImageCount = overlayContext.minImageCount;
    overlayInfo.imageCount = overlayContext.imageCount;
    overlayInfo.msaaSamples = overlayContext.sampleCount;
    overlayInfo.colorFormat = overlayContext.colorFormat;
    ui = std::make_unique<Tasrovy::UI::UIOverlay>(overlayInfo);
    resourceMonitor = std::make_unique<ResourceMonitor>();
    gpuTimingNamesPerFrame.resize(maxFrames);
}

SceneRendererComponents::~SceneRendererComponents() = default;

void SceneRendererComponents::resetSceneTransientState() {
    taffyStressSource.reset();
    animatedTaffy = nullptr;
    taffyYawOffset = 0.0f;
    lastTaffyAnimationTime = {};
}

} // namespace Tasrovy::Renderer
