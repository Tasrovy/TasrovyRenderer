#include "SceneUpdateCoordinator.h"

#include "RenderScene.h"
#include "../render/Scene.h"

namespace Tasrovy::Renderer {

SceneUpdateCoordinator::SceneUpdateCoordinator(RenderScene& renderScene)
    : renderScene_(renderScene) {}

SceneUpdateCoordinator::Update SceneUpdateCoordinator::synchronize(
    const PipelineEvaluator& evaluatePipeline) {
    const auto snapshot = renderScene_.snapshot();
    Update update{};
    update.pipeline = snapshot.pipeline;
    update.version = snapshot.version;
    if (!snapshot.scene) return update;

    const bool pipelineRequiresRebuild =
        evaluatePipeline ? evaluatePipeline(snapshot.pipeline) : false;
    if (snapshot.dirty || !activeScene_) {
        activeScene_ = snapshot.scene->clone();
        update.rebuildRequired = true;
        update.acknowledgesDirtyVersion = snapshot.dirty;
    } else {
        update.rebuildRequired = pipelineRequiresRebuild;
    }
    update.scene = activeScene_;
    return update;
}

void SceneUpdateCoordinator::acknowledge(const Update& update) {
    if (update.acknowledgesDirtyVersion) {
        renderScene_.acknowledge(update.version);
    }
}

std::shared_ptr<Tasrovy::Render::PipelineBase>
SceneUpdateCoordinator::currentPipeline() const {
    return renderScene_.snapshot().pipeline;
}

void SceneUpdateCoordinator::adoptPipelineIfEmpty(
    const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline) {
    renderScene_.adoptPipelineIfEmpty(pipeline);
}

} // namespace Tasrovy::Renderer
