#pragma once

#include "FrameBufferUpload.h"
#include "../RHI/FrameExecutor.h"
#include "../RHI/RenderFramePlan.h"
#include "../render/FramePacket.h"

#include <cstdint>
#include <vector>

namespace Tasrovy::Renderer {

// Immutable-by-convention handoff from the render thread to the RHI thread.
// It contains every frame-owned value needed after publication, including a
// token that owns a cloned ImGui DrawData snapshot. The RHI task must not
// retain mutable Scene or render-thread working-state references.
struct RenderFrameSubmission {
    uint64_t frameNumber = 0;
    uint32_t expectedFrameIndex = 0;
    Tasrovy::Render::FramePacket packet;
    Tasrovy::RHI::RenderFrameExecutionPlan executionPlan;
    Tasrovy::RHI::FrameExecutionBindings bindings;
    std::vector<FrameBufferUpload> bufferUploads;
    uint64_t overlayFrameToken = 0;
};

} // namespace Tasrovy::Renderer
