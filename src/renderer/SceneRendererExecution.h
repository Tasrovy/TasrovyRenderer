#pragma once

#include "RenderScene.h"
#include "RenderThread.h"
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
        RenderThread& renderThread);
    ~SceneRendererExecution();

    SceneRendererExecution(const SceneRendererExecution&) = delete;
    SceneRendererExecution& operator=(const SceneRendererExecution&) = delete;

    void run();

private:
    void drawSceneDebugUI();
    void renderLoop();
    void processScene(const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void rebuildDisplayResources(Tasrovy::Render::PipelineBase& pipeline);
    void renderFrame(Tasrovy::Render::Scene& scene);

    Tasrovy::Windowing::Window& window_;
    uint32_t maxFramesInFlight_;

    SceneRendererComponents* renderState_ = nullptr;
    RenderScene& renderScene_;
    RenderThread& renderThread_;
};

} // namespace Tasrovy::Renderer
