#include "FrameOrchestrator.h"

#include "../render/Pipeline.h"
#include "../render/Scene.h"

namespace Tasrovy::Renderer {

void FrameOrchestrator::rebuild(
    const std::shared_ptr<Tasrovy::Render::Scene>& scene,
    Tasrovy::Render::PipelineBase& pipeline,
    uint64_t frameNumber) {
    if (!scene) {
        return;
    }
    pipeline.GenPass(scene);
    renderGraph_ = Tasrovy::Render::RenderGraph::compile(pipeline);
    resetTemporalHistory();
    compileFrame(*scene, pipeline, frameNumber);
}

void FrameOrchestrator::compileFrame(
    Tasrovy::Render::Scene& scene,
    Tasrovy::Render::PipelineBase& pipeline,
    uint64_t frameNumber) {
    framePacket_ = frameCompiler_.compile(
        scene, pipeline, renderGraph_, frameNumber);
    executionPlan_ = framePlanCompiler_.compile(framePacket_);
}

void FrameOrchestrator::resetTemporalHistory() {
    frameCompiler_.resetHistory();
}

} // namespace Tasrovy::Renderer
