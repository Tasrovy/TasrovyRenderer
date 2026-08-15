#include "FrameExecutionScheduler.h"

#include <unordered_map>
#include <unordered_set>

namespace Tasrovy::Renderer {

ScheduledFramePasses FrameExecutionScheduler::schedule(
    Render::FramePacket& framePacket,
    const RHI::RenderFrameExecutionPlan& executionPlan) const {
    ScheduledFramePasses result{};
    result.orderedPasses.reserve(framePacket.passes.size());
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

    std::unordered_map<uint64_t, Render::FramePassPacket*> packetById;
    packetById.reserve(framePacket.passes.size());
    for (auto& pass : framePacket.passes) {
        if (pass.id != 0) {
            packetById.emplace(pass.id, &pass);
        }
    }

    std::unordered_set<Render::FramePassPacket*> selected;
    selected.reserve(framePacket.passes.size());
    for (const auto& plannedPass : executionPlan.passes) {
        const auto packet = packetById.find(plannedPass.passId);
        if (packet != packetById.end() &&
            selected.insert(packet->second).second) {
            result.orderedPasses.push_back(packet->second);
        }
    }

    for (const auto& diagnostic : executionPlan.diagnostics) {
        if (!result.diagnostics.empty()) {
            result.diagnostics += '\n';
        }
        result.diagnostics += diagnostic;
    }

    if (result.orderedPasses.size() != framePacket.passes.size()) {
        if (!result.diagnostics.empty()) {
            result.diagnostics += '\n';
        }
        result.diagnostics +=
            "RHI frame plan scheduled " +
            std::to_string(result.orderedPasses.size()) + " of " +
            std::to_string(framePacket.passes.size()) +
            " executable passes; using Render Graph order";
        result.orderedPasses.clear();
        for (auto& pass : framePacket.passes) {
            result.orderedPasses.push_back(&pass);
        }
    }
    return result;
}

} // namespace Tasrovy::Renderer
