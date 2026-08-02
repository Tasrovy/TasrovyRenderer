#include "SceneRenderer.h"

#include "SceneRendererRuntime.h"

#include <utility>

namespace Tasrovy::Renderer {

SceneRenderer::SceneRenderer(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFramesInFlight)
    : runtime_(std::make_unique<SceneRendererRuntime>(
          window,
          maxFramesInFlight)) {}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::setScene(std::shared_ptr<Tasrovy::Render::Scene> scene) {
    runtime_->setScene(std::move(scene));
}

void SceneRenderer::setPipeline(
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline) {
    runtime_->setPipeline(std::move(pipeline));
}

void SceneRenderer::addPrimitive(const Tasrovy::Render::Object& object) {
    runtime_->addPrimitive(object);
}

void SceneRenderer::updatePrimitive(const Tasrovy::Render::Object& object) {
    runtime_->updatePrimitive(object);
}

void SceneRenderer::removePrimitive(const std::string& name) {
    runtime_->removePrimitive(name);
}

void SceneRenderer::start() {
    runtime_->start();
}

void SceneRenderer::stop() {
    runtime_->stop();
}

bool SceneRenderer::isRunning() const {
    return runtime_->isRunning();
}

} // namespace Tasrovy::Renderer
