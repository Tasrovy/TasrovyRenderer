#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace Tasrovy::Render {
class PipelineBase;
class Scene;
}

namespace Tasrovy::Renderer {

class RenderScene;

// Single render-thread entry point for consuming published scene snapshots.
// It owns the mutable render-thread scene clone and keeps Dirty/Version
// acknowledgement ordered after a successful structural rebuild.
class SceneUpdateCoordinator {
public:
    struct Update {
        std::shared_ptr<Tasrovy::Render::Scene> scene;
        std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline;
        bool rebuildRequired = false;
        bool acknowledgesDirtyVersion = false;
        uint64_t version = 0;
    };

    using PipelineEvaluator = std::function<bool(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>&)>;

    explicit SceneUpdateCoordinator(RenderScene& renderScene);

    Update synchronize(const PipelineEvaluator& evaluatePipeline);
    void acknowledge(const Update& update);

    std::shared_ptr<Tasrovy::Render::PipelineBase> currentPipeline() const;
    void adoptPipelineIfEmpty(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline);

private:
    RenderScene& renderScene_;
    std::shared_ptr<Tasrovy::Render::Scene> activeScene_;
};

} // namespace Tasrovy::Renderer
