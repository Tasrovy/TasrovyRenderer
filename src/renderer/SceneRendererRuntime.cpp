#include "SceneRendererRuntime.h"

#include "SceneRendererExecution.h"

#include <utility>

namespace Tasrovy::Renderer {

SceneRendererRuntime::SceneRendererRuntime(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFramesInFlight)
    : components_(window, maxFramesInFlight),
      execution_(std::make_unique<SceneRendererExecution>(
          window,
          maxFramesInFlight,
          components_,
          renderScene_,
          renderThread_)) {}

SceneRendererRuntime::~SceneRendererRuntime() {
    stop();
}

void SceneRendererRuntime::setScene(
    std::shared_ptr<Tasrovy::Render::Scene> scene) {
    components_.resetSceneTransientState();
    renderScene_.submitScene(std::move(scene));
}

void SceneRendererRuntime::setPipeline(
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline) {
    renderScene_.submitPipeline(std::move(pipeline));
}

void SceneRendererRuntime::addPrimitive(
    const Tasrovy::Render::Object& object) {
    renderScene_.addPrimitive(object);
}

void SceneRendererRuntime::updatePrimitive(
    const Tasrovy::Render::Object& object) {
    renderScene_.updatePrimitive(object);
}

void SceneRendererRuntime::removePrimitive(const std::string& name) {
    renderScene_.removePrimitive(name);
}

void SceneRendererRuntime::start() {
    renderThread_.start([this]() { execution_->run(); });
}

void SceneRendererRuntime::stop() {
    renderThread_.stop();
}

bool SceneRendererRuntime::isRunning() const {
    return renderThread_.running();
}

} // namespace Tasrovy::Renderer
