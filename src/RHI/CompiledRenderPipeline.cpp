#include "CompiledRenderPipeline.h"

#include <utility>

namespace Tasrovy::RHI {

void CompiledRenderPipeline::reset() {
    passes_.clear();
}

CompiledPassResources& CompiledRenderPipeline::add(
    CompiledPassResources resources) {
    passes_.push_back(std::move(resources));
    return passes_.back();
}

CompiledPassResources* CompiledRenderPipeline::find(uint64_t passId) {
    for (auto& pass : passes_) {
        if (pass.passId == passId) {
            return &pass;
        }
    }
    return nullptr;
}

const CompiledPassResources* CompiledRenderPipeline::find(
    uint64_t passId) const {
    for (const auto& pass : passes_) {
        if (pass.passId == passId) {
            return &pass;
        }
    }
    return nullptr;
}

CompiledRenderPipeline::Container& CompiledRenderPipeline::passes() {
    return passes_;
}

const CompiledRenderPipeline::Container& CompiledRenderPipeline::passes() const {
    return passes_;
}

bool CompiledRenderPipeline::empty() const {
    return passes_.empty();
}

size_t CompiledRenderPipeline::size() const {
    return passes_.size();
}

} // namespace Tasrovy::RHI
