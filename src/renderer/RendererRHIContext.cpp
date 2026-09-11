#include "RendererRHIContext.h"

#include "../RHI/CommandList.h"
#include "../RHI/FrameScheduler.h"
#include "../window/Window.h"

#include <algorithm>

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
    device->getFrameScheduler().waitForInFlightFrames();
    if (externalFeatureExecutor) {
        externalFeatureExecutor->invalidateResources();
        externalFeatureExecutor.reset();
    }
    device->destroyResourceScope(displayResourceScope);
    device->destroyResourceScope(sceneResourceScope);
    device->destroyResourceScope(persistentResourceScope);
}

} // namespace Tasrovy::Renderer
