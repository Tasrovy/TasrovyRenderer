#pragma once

#include "RenderScene.h"
#include "RenderThread.h"
#include "RHIThread.h"
#include "SceneRendererComponents.h"
#include "SceneUpdateCoordinator.h"

#include <cstdint>
#include <deque>
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

class RendererDebugUI;

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
    void renderLoop();
    void applySceneUpdates(
        const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void rebuildRenderGraph(
        const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void rebuildDisplayResources(Tasrovy::Render::PipelineBase& pipeline);
    void renderFrame(Tasrovy::Render::Scene& scene);
    bool pollPendingRHIFrame();
    bool waitForPendingRHIFrame();
    bool consumeOldestRHIFrame(bool wait);

    Tasrovy::Windowing::Window& window_;
    uint32_t maxFramesInFlight_;

    SceneRendererComponents* renderState_ = nullptr;
    RenderThread& renderThread_;
    RHIThread& rhiThread_;
    SceneUpdateCoordinator sceneUpdates_;
    std::unique_ptr<RendererDebugUI> debugUI_;
    struct PendingRHIFrame;
    std::deque<std::unique_ptr<PendingRHIFrame>> pendingRHIFrames_;
    uint32_t nextRHIFrameIndex_ = 0;
    bool pendingSwapchainRebuild_ = false;
    // Accessed only by tasks running serially on RHIThread. A failed acquire
    // invalidates every later speculative submission until swapchain recovery.
    bool rhiFrameSequenceValid_ = true;
};

} // namespace Tasrovy::Renderer
