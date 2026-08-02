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
}

RendererRHIContext::~RendererRHIContext() {
    if (!device) {
        return;
    }
    device->getFrameScheduler().waitForInFlightFrames();
    device->destroyResourceScope(displayResourceScope);
    device->destroyResourceScope(sceneResourceScope);
    device->destroyResourceScope(persistentResourceScope);
}

} // namespace Tasrovy::Renderer
