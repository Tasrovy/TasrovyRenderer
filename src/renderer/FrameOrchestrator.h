#pragma once

#include "../RHI/RenderFramePlan.h"
#include "../render/FrameCompiler.h"
#include "../render/FramePacket.h"
#include "../render/RenderGraph.h"

#include <cstdint>
#include <memory>

namespace Tasrovy::Render {
class PipelineBase;
class Scene;
}

namespace Tasrovy::Renderer {

// Compiles high-level scene and pass descriptions into a backend-independent
// frame packet, then asks RHI to derive lifetime and transition execution
// plans. This is the renderer-side equivalent of the RDG setup/compile stage.
class FrameOrchestrator {
public:
    void rebuild(
        const std::shared_ptr<Tasrovy::Render::Scene>& scene,
        Tasrovy::Render::PipelineBase& pipeline,
        uint64_t frameNumber);

    void compileFrame(
        Tasrovy::Render::Scene& scene,
        Tasrovy::Render::PipelineBase& pipeline,
        uint64_t frameNumber);

    void resetTemporalHistory();

    const Tasrovy::Render::RenderGraph& renderGraph() const {
        return renderGraph_;
    }

    const Tasrovy::Render::FramePacket& framePacket() const {
        return framePacket_;
    }
    Tasrovy::Render::FramePacket& framePacket() { return framePacket_; }
    const Tasrovy::Render::FrameSourceRegistry& sourceRegistry() const {
        return frameCompiler_.sourceRegistry();
    }

    const Tasrovy::RHI::RenderFrameExecutionPlan& executionPlan() const {
        return executionPlan_;
    }

private:
    Tasrovy::Render::RenderGraph renderGraph_;
    Tasrovy::Render::FrameCompiler frameCompiler_;
    Tasrovy::Render::FramePacket framePacket_;
    Tasrovy::RHI::RHIFrameCompiler framePlanCompiler_;
    Tasrovy::RHI::RenderFrameExecutionPlan executionPlan_;
};

} // namespace Tasrovy::Renderer
