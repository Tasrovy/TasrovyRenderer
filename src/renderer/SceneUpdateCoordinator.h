#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "RenderScene.h"

namespace Tasrovy::Render {
class PipelineBase;
class Scene;
}

namespace Tasrovy::Renderer {

// Single render-thread entry point for consuming published scene snapshots.
// It owns the mutable render-thread scene and uses independent generations to
// distinguish structural rebuilds from data-only updates.
class SceneUpdateCoordinator {
public:
    struct Update {
        std::shared_ptr<Tasrovy::Render::Scene> scene;
        std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline;
        SceneVersions versions;
        bool structuralChanged = false;
        bool transformChanged = false;
        bool materialChanged = false;
        bool lightingChanged = false;
        bool pipelineChanged = false;
        bool rebuildRequired = false;
    };

    using PipelineEvaluator = std::function<bool(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>&)>;

    explicit SceneUpdateCoordinator(RenderScene& renderScene);

    Update synchronize(const PipelineEvaluator& evaluatePipeline);

    std::shared_ptr<Tasrovy::Render::PipelineBase> currentPipeline() const;
    void adoptPipelineIfEmpty(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline);

private:
    RenderScene& renderScene_;
    std::shared_ptr<Tasrovy::Render::Scene> activeScene_;
    SceneVersions appliedVersions_{};
};

} // namespace Tasrovy::Renderer
