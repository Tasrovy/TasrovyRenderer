#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Tasrovy::Windowing { class Window; }

namespace Tasrovy {
namespace Render {
class Scene;
class PipelineBase;
class Object;
}
}

namespace Tasrovy::Renderer {

class SceneRendererRuntime;

// Public rendering facade. Like an engine renderer module entry point, this
// class only owns the runtime coordinator and forwards scene-facing requests;
// frame graph construction and RHI execution remain internal implementation.
class SceneRenderer {
public:
    explicit SceneRenderer(Tasrovy::Windowing::Window& window, uint32_t maxFramesInFlight = 2);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void setScene(std::shared_ptr<Tasrovy::Render::Scene> scene);
    void setPipeline(std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline);
    void addPrimitive(const Tasrovy::Render::Object& object);
    void updatePrimitive(const Tasrovy::Render::Object& object);
    void removePrimitive(const std::string& name);

    void start();
    void stop();
    bool isRunning() const;

private:
    std::unique_ptr<SceneRendererRuntime> runtime_;
};

} // namespace Tasrovy::Renderer
