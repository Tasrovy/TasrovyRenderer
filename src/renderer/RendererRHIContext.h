#pragma once

#include "../RHI/Device.h"
#include "../RHI/FrameExecutor.h"

#include <cstdint>
#include <memory>

namespace Tasrovy::RHI {
class CommandList;
}

namespace Tasrovy::Windowing {
class Window;
}

namespace Tasrovy::Renderer {

// Owns the RHI lifetime used by the renderer runtime. SceneRenderer schedules
// frames; this system owns devices, command recording and resource scopes.
class RendererRHIContext {
public:
    RendererRHIContext(
        Tasrovy::Windowing::Window& window,
        uint32_t maxFramesInFlight);
    ~RendererRHIContext();

    RendererRHIContext(const RendererRHIContext&) = delete;
    RendererRHIContext& operator=(const RendererRHIContext&) = delete;

    std::shared_ptr<Tasrovy::RHI::Device> device;
    std::shared_ptr<Tasrovy::RHI::CommandList> commandList;
    Tasrovy::RHI::FrameExecutor frameExecutor;
    // Installed by an optional SDK adapter. Null keeps external-feature passes
    // on their deterministic shader fallback path.
    std::unique_ptr<Tasrovy::RHI::IExternalFeatureExecutor>
        externalFeatureExecutor;
    Tasrovy::RHI::Device::ResourceScope persistentResourceScope = 0;
    Tasrovy::RHI::Device::ResourceScope sceneResourceScope = 0;
    Tasrovy::RHI::Device::ResourceScope displayResourceScope = 0;
};

} // namespace Tasrovy::Renderer
