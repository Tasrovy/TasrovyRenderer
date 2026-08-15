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

class SceneRendererExecution;

// Renderer composition root. It owns service lifetimes and wires scene
// submission, the render thread and frame execution together. Rendering
// policies, graph compilation and command generation belong to components.
class SceneRendererRuntime {
public:
    SceneRendererRuntime(
        Tasrovy::Windowing::Window& window,
        uint32_t maxFramesInFlight);
    ~SceneRendererRuntime();

    SceneRendererRuntime(const SceneRendererRuntime&) = delete;
    SceneRendererRuntime& operator=(const SceneRendererRuntime&) = delete;

    void setScene(std::shared_ptr<Tasrovy::Render::Scene> scene);
    void setPipeline(std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline);
    void addPrimitive(const Tasrovy::Render::Object& object);
    void updatePrimitive(const Tasrovy::Render::Object& object);
    void removePrimitive(const std::string& name);

    void start();
    void stop();
    bool isRunning() const;

private:
    RenderScene renderScene_;
    RenderThread renderThread_;
    RHIThread rhiThread_;
    SceneRendererComponents components_;
    std::unique_ptr<SceneRendererExecution> execution_;
};

} // namespace Tasrovy::Renderer
