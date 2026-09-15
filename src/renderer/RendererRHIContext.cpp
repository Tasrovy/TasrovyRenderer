#include "RendererRHIContext.h"

#include "../RHI/CommandList.h"
#include "../RHI/FrameScheduler.h"
#include "../window/Window.h"
#include "Logger.hpp"

#include <algorithm>
#include <exception>

namespace Tasrovy::Renderer {

RendererRHIContext::RendererRHIContext(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFramesInFlight) {
    device = Tasrovy::RHI::Device::createForSurface({
        window.getHandle(),
        static_cast<uint32_t>(std::max(window.getWidth(), 1)),
        static_cast<uint32_t>(std::max(window.getHeight(), 1)),
        maxFramesInFlight
    });
    persistentResourceScope = device->createResourceScope();
    sceneResourceScope = device->createResourceScope();
    displayResourceScope = device->createResourceScope();
    commandList = device->retainResource(
        persistentResourceScope,
        device->createCommandList());
    externalFeatureExecutor =
        Tasrovy::RHI::createExternalFeatureExecutor(*device);
}

RendererRHIContext::~RendererRHIContext() {
    if (!device) {
        return;
    }
    LOG_INFO("Shutdown: waiting for renderer frame fences");
    try {
        device->getFrameScheduler().waitForInFlightFrames();
        LOG_INFO("Shutdown: renderer frame fences completed");
    } catch (const std::exception& error) {
        LOG_ERROR(
            "Shutdown: renderer fence drain failed: {}",
            error.what());
    }
    // Frame fences do not cover the presentation operation queued after the
    // graphics submit. Idle the complete device before NGX, render resources,
    // swapchain images, or presentation semaphores can be released.
    device->waitIdleForShutdown();
    if (externalFeatureExecutor) {
        externalFeatureExecutor->invalidateResources();
        externalFeatureExecutor.reset();
    }
    device->destroyResourceScope(displayResourceScope);
    device->destroyResourceScope(sceneResourceScope);
    device->destroyResourceScope(persistentResourceScope);
}

} // namespace Tasrovy::Renderer
