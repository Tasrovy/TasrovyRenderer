#pragma once

#include "PipelineResource.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Tasrovy::Render {

class PipelineBase;
class PipelinePass;

enum class RenderGraphHazard {
    ReadAfterWrite,
    WriteAfterRead,
    WriteAfterWrite,
    Explicit
};

struct RenderGraphEdge {
    size_t producer = 0;
    size_t consumer = 0;
    std::string resource;
    RenderGraphHazard hazard = RenderGraphHazard::ReadAfterWrite;
};

struct RenderGraphNode {
    std::shared_ptr<PipelinePass> pass;
    size_t declarationIndex = 0;
    std::vector<PipelineResourceRef> reads;
    std::vector<PipelineResourceRef> writes;
};

struct RenderGraphResourceLifetime {
    std::string resource;
    size_t firstUse = 0;
    size_t lastUse = 0;
    bool external = false;
    bool crossFrame = false;
    bool buffer = false;
};

class RenderGraph {
public:
    // Compiles a dependency graph independently of PipelineBase::addPass()
    // order. Reads of multi-writer resources must identify producerPass.
    static RenderGraph compile(const PipelineBase& pipeline);

    bool isValid() const;
    const std::vector<RenderGraphNode>& getNodes() const;
    const std::vector<RenderGraphEdge>& getEdges() const;
    const std::vector<RenderGraphResourceLifetime>& getResourceLifetimes() const;
    const std::vector<std::string>& getDiagnostics() const;

private:
    bool valid_ = true;
    std::vector<RenderGraphNode> nodes_;
    std::vector<RenderGraphEdge> edges_;
    std::vector<RenderGraphResourceLifetime> resourceLifetimes_;
    std::vector<std::string> diagnostics_;
};

const char* renderGraphHazardName(RenderGraphHazard hazard);

} // namespace Tasrovy::Render
