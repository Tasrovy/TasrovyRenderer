#include "SceneRendererRuntime.h"

#include "SceneRendererExecution.h"

#include <utility>

namespace Tasrovy::Renderer {

SceneRendererRuntime::SceneRendererRuntime(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFramesInFlight)
    : rhiThread_(maxFramesInFlight),
      components_(window, maxFramesInFlight),
      execution_(std::make_unique<SceneRendererExecution>(
          window,
          maxFramesInFlight,
          components_,
          renderScene_,
          renderThread_,
          rhiThread_)) {}

SceneRendererRuntime::~SceneRendererRuntime() {
    stop();
}

void SceneRendererRuntime::setScene(
    std::shared_ptr<Tasrovy::Render::Scene> scene) {
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
    rhiThread_.start();
    renderThread_.start([this]() { execution_->run(); });
}

void SceneRendererRuntime::stop() {
    renderThread_.stop();
    rhiThread_.stop();
}

bool SceneRendererRuntime::isRunning() const {
    return renderThread_.running();
}

} // namespace Tasrovy::Renderer
