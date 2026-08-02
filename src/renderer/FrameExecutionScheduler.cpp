#include "FrameExecutionScheduler.h"

#include "../RHI/CompiledRenderPipeline.h"

#include <unordered_set>

namespace Tasrovy::Renderer {

ScheduledFramePasses FrameExecutionScheduler::schedule(
    RHI::CompiledRenderPipeline& compiledPipeline,
    const RHI::RenderFrameExecutionPlan& executionPlan) const {
    ScheduledFramePasses result{};
    result.orderedPasses.reserve(compiledPipeline.size());
    result.executionById.reserve(executionPlan.passes.size());

    if (!executionPlan.valid()) {
        for (const auto& diagnostic : executionPlan.diagnostics) {
            if (!result.diagnostics.empty()) {
                result.diagnostics += '\n';
            }
            result.diagnostics += diagnostic;
        }
        return result;
    }

    for (const auto& plannedPass : executionPlan.passes) {
        result.executionById.emplace(plannedPass.passId, &plannedPass);
    }

    std::unordered_map<uint64_t, RHI::CompiledPassResources*> compiledById;
    compiledById.reserve(compiledPipeline.size());
    for (auto& pass : compiledPipeline.passes()) {
        if (pass.passId != 0) {
            compiledById.emplace(pass.passId, &pass);
        }
    }

    std::unordered_set<RHI::CompiledPassResources*> selected;
    selected.reserve(compiledPipeline.size());
    for (const auto& plannedPass : executionPlan.passes) {
        const auto compiled = compiledById.find(plannedPass.passId);
        if (compiled != compiledById.end() &&
            selected.insert(compiled->second).second) {
            result.orderedPasses.push_back(compiled->second);
        }
    }

    for (const auto& diagnostic : executionPlan.diagnostics) {
        if (!result.diagnostics.empty()) {
            result.diagnostics += '\n';
        }
        result.diagnostics += diagnostic;
    }

    if (result.orderedPasses.size() != compiledPipeline.size()) {
        if (!result.diagnostics.empty()) {
            result.diagnostics += '\n';
        }
        result.diagnostics +=
            "RHI frame plan scheduled " +
            std::to_string(result.orderedPasses.size()) + " of " +
            std::to_string(compiledPipeline.size()) +
            " executable passes; using Render Graph order";
        result.orderedPasses.clear();
        for (auto& pass : compiledPipeline.passes()) {
            result.orderedPasses.push_back(&pass);
        }
    }
    return result;
}

} // namespace Tasrovy::Renderer
