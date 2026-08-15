#pragma once

#include "../RHI/RenderFramePlan.h"
#include "../render/FramePacket.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::Renderer {

struct ScheduledFramePasses {
    std::vector<Render::FramePassPacket*> orderedPasses;
    std::unordered_map<
        uint64_t,
        const RHI::RenderPassExecutionPlan*> executionById;
    std::string diagnostics;
};

class FrameExecutionScheduler {
public:
    ScheduledFramePasses schedule(
        Render::FramePacket& framePacket,
        const RHI::RenderFrameExecutionPlan& executionPlan) const;
};

} // namespace Tasrovy::Renderer
