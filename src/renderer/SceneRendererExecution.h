#pragma once

#include "RenderScene.h"
#include "RenderThread.h"
#include "RHIThread.h"
#include "SceneRendererComponents.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Tasrovy::Windowing {
class Window;
}

namespace Tasrovy::Render {
class Object;
class PipelineBase;
class Scene;
}

namespace Tasrovy::Renderer {

// Performs scene compilation and frame execution against runtime-owned scene
// and thread services. This is an implementation component, not a composition
// root and not part of the public renderer API.
class SceneRendererExecution {
public:
    SceneRendererExecution(
        Tasrovy::Windowing::Window& window,
        uint32_t maxFramesInFlight,
        SceneRendererComponents& components,
        RenderScene& renderScene,
        RenderThread& renderThread,
        RHIThread& rhiThread);
    ~SceneRendererExecution();

    SceneRendererExecution(const SceneRendererExecution&) = delete;
    SceneRendererExecution& operator=(const SceneRendererExecution&) = delete;

    void run();

private:
    void drawSceneDebugUI();
    void renderLoop();
    void applySceneUpdates(
        const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void rebuildRenderGraph(
        const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void rebuildDisplayResources(Tasrovy::Render::PipelineBase& pipeline);
    void renderFrame(Tasrovy::Render::Scene& scene);
    bool waitForPendingRHIFrame();

    Tasrovy::Windowing::Window& window_;
    uint32_t maxFramesInFlight_;

    SceneRendererComponents* renderState_ = nullptr;
    RenderScene& renderScene_;
    RenderThread& renderThread_;
    RHIThread& rhiThread_;
    struct PendingRHIFrame;
    std::unique_ptr<PendingRHIFrame> pendingRHIFrame_;
    // Render-thread-owned mutable working copy. Published RenderScene
    // snapshots remain immutable and are never animated or resized in place.
    std::shared_ptr<Tasrovy::Render::Scene> activeScene_;
};

} // namespace Tasrovy::Renderer
