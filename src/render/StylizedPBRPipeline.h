#pragma once

#include "Pipeline.h"

namespace Tasrovy::Render {

// Incremental destination for the PBR + NPR pipeline reconstructed from the
// reference RenderDoc capture. It starts from a known-good compatibility graph
// and replaces stages without modifying DeferredPipeline.
class StylizedPBRPipeline final : public PipelineBase {
public:
    static std::shared_ptr<StylizedPBRPipeline> create(
        const std::string& name = "StylizedPBR");

    bool applyConfiguration(
        const PipelineConfiguration& configuration) override;
    void GenPass(std::shared_ptr<Scene> scene) override;

private:
    StylizedPBRPipeline() = default;
    explicit StylizedPBRPipeline(const std::string& name);
};

} // namespace Tasrovy::Render
