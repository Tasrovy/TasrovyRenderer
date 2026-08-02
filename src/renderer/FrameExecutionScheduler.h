#pragma once

#include "../RHI/RenderFramePlan.h"
#include "../RHI/CompiledRenderPipeline.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::Renderer {

struct ScheduledFramePasses {
    std::vector<RHI::CompiledPassResources*> orderedPasses;
    std::unordered_map<
        uint64_t,
        const RHI::RenderPassExecutionPlan*> executionById;
    std::string diagnostics;
};

class FrameExecutionScheduler {
public:
    ScheduledFramePasses schedule(
        RHI::CompiledRenderPipeline& compiledPipeline,
        const RHI::RenderFrameExecutionPlan& executionPlan) const;
};

} // namespace Tasrovy::Renderer
