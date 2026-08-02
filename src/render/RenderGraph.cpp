#include "RenderGraph.h"

#include "Pipeline.h"
#include "PipelinePass.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace Tasrovy::Render {

namespace {

constexpr size_t InvalidNode = std::numeric_limits<size_t>::max();

struct EdgeKey {
    size_t producer = 0;
    size_t consumer = 0;
    std::string resource;
    RenderGraphHazard hazard = RenderGraphHazard::ReadAfterWrite;
    bool operator==(const EdgeKey&) const = default;
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& key) const {
        size_t hash = std::hash<size_t>{}(key.producer);
        const auto combine = [&](size_t value) {
            hash ^= value + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        };
        combine(std::hash<size_t>{}(key.consumer));
        combine(std::hash<std::string>{}(key.resource));
        combine(std::hash<int>{}(static_cast<int>(key.hazard)));
        return hash;
    }
};

} // namespace

RenderGraph RenderGraph::compile(const PipelineBase& pipeline) {
    RenderGraph graph;
    graph.diagnostics_ = pipeline.validatePassDependencies();
    graph.valid_ = graph.diagnostics_.empty();

    const auto& passes = pipeline.getPasses();
    std::vector<RenderGraphNode> declaredNodes(passes.size());
    std::vector<std::vector<size_t>> adjacency(passes.size());
    std::vector<size_t> indegree(passes.size(), 0);
    std::unordered_map<std::string, size_t> passIndices;
    std::unordered_map<std::string, std::vector<size_t>> writersByResource;
    std::unordered_set<std::string> externalResources;
    std::unordered_set<EdgeKey, EdgeKeyHash> edgeKeys;

    for (const auto& texture : pipeline.getTextures()) {
        if (texture.external) {
            externalResources.insert(texture.name);
        }
    }
    for (const auto& buffer : pipeline.getBuffers()) {
        if (buffer.external || buffer.hostVisible) {
            externalResources.insert(buffer.name);
        }
    }

    const auto addDiagnostic = [&](std::string message) {
        if (std::find(
                graph.diagnostics_.begin(),
                graph.diagnostics_.end(),
                message) == graph.diagnostics_.end()) {
            graph.diagnostics_.push_back(std::move(message));
        }
        graph.valid_ = false;
    };

    const auto addEdge = [&](size_t producer,
                             size_t consumer,
                             const std::string& resource,
                             RenderGraphHazard hazard) {
        if (producer == consumer) {
            return;
        }
        const EdgeKey key{producer, consumer, resource, hazard};
        if (!edgeKeys.insert(key).second) {
            return;
        }
        graph.edges_.push_back({producer, consumer, resource, hazard});
        adjacency[producer].push_back(consumer);
        ++indegree[consumer];
    };

    // Phase one only collects declarations. No dependency is inferred from
    // the order in which PipelineBase::addPass() was called.
    for (size_t index = 0; index < passes.size(); ++index) {
        const auto& pass = passes[index];
        if (!pass) {
            graph.valid_ = false;
            continue;
        }
        auto& node = declaredNodes[index];
        node = {pass, index, pass->getReadResources(), pass->getWriteResources()};
        passIndices.emplace(pass->getName(), index);
        for (const auto& write : node.writes) {
            writersByResource[write.resource].push_back(index);
        }
    }

    // Explicit execution dependencies describe ordering that does not follow
    // from a unique resource producer (for example mutually exclusive passes
    // that share one physical history target).
    for (size_t consumer = 0; consumer < passes.size(); ++consumer) {
        const auto& pass = passes[consumer];
        if (!pass) {
            continue;
        }
        for (const auto& producerName : pass->getExecutionDependencies()) {
            const auto producer = passIndices.find(producerName);
            if (producer == passIndices.end()) {
                addDiagnostic(
                    "Pass '" + pass->getName() +
                    "' depends on unknown pass '" + producerName + "'");
                continue;
            }
            if (producer->second == consumer) {
                addDiagnostic(
                    "Pass '" + pass->getName() +
                    "' cannot depend on itself");
                continue;
            }
            addEdge(
                producer->second,
                consumer,
                "<execution>",
                RenderGraphHazard::Explicit);
        }
    }

    struct ResolvedRead {
        size_t reader = InvalidNode;
        size_t producer = InvalidNode;
        std::string resource;
    };
    std::vector<ResolvedRead> resolvedReads;

    // Phase two resolves every current-frame read to a producer. A single
    // writer is inferred automatically. Multi-writer resources must name the
    // producer pass so the requested logical version is unambiguous.
    for (size_t consumer = 0; consumer < declaredNodes.size(); ++consumer) {
        const auto& node = declaredNodes[consumer];
        if (!node.pass) {
            continue;
        }
        for (const auto& read : node.reads) {
            if (read.previousFrame) {
                continue;
            }

            size_t producerIndex = InvalidNode;
            if (!read.producerPass.empty()) {
                const auto producer = passIndices.find(read.producerPass);
                if (producer == passIndices.end()) {
                    addDiagnostic(
                        "Pass '" + node.pass->getName() +
                        "' reads texture '" + read.resource +
                        "' from unknown producer pass '" +
                        read.producerPass + "'");
                    continue;
                }
                producerIndex = producer->second;
                const auto writers = writersByResource.find(read.resource);
                const bool producerWritesResource =
                    writers != writersByResource.end() &&
                    std::find(
                        writers->second.begin(),
                        writers->second.end(),
                        producerIndex) != writers->second.end();
                if (!producerWritesResource) {
                    addDiagnostic(
                        "Pass '" + node.pass->getName() +
                        "' names pass '" + read.producerPass +
                        "' as producer of texture '" + read.resource +
                        "', but that pass does not write it");
                    continue;
                }
            } else {
                const auto writers = writersByResource.find(read.resource);
                if (writers != writersByResource.end()) {
                    for (const size_t writer : writers->second) {
                        if (writer == consumer) {
                            continue;
                        }
                        if (producerIndex != InvalidNode) {
                            producerIndex = InvalidNode;
                            addDiagnostic(
                                "Pass '" + node.pass->getName() +
                                "' reads multi-writer texture '" +
                                read.resource +
                                "' without naming a producer pass");
                            break;
                        }
                        producerIndex = writer;
                    }
                }
            }

            if (producerIndex == InvalidNode) {
                if (!externalResources.contains(read.resource) &&
                    read.producerPass.empty()) {
                    const auto writers = writersByResource.find(read.resource);
                    const bool hasOtherWriters =
                        writers != writersByResource.end() &&
                        std::any_of(
                            writers->second.begin(),
                            writers->second.end(),
                            [consumer](size_t writer) {
                                return writer != consumer;
                            });
                    if (!hasOtherWriters) {
                        addDiagnostic(
                            "Pass '" + node.pass->getName() +
                            "' reads texture '" + read.resource +
                            "' without a current-frame producer");
                    }
                }
                continue;
            }
            if (producerIndex == consumer) {
                addDiagnostic(
                    "Pass '" + node.pass->getName() +
                    "' cannot read its own newly written version of texture '" +
                    read.resource + "'");
                continue;
            }

            addEdge(
                producerIndex,
                consumer,
                read.resource,
                RenderGraphHazard::ReadAfterWrite);
            resolvedReads.push_back({
                consumer,
                producerIndex,
                read.resource
            });
        }
    }

    const auto hasPath = [&](size_t from, size_t to) {
        if (from == to) {
            return true;
        }
        std::vector<bool> visited(passes.size(), false);
        std::vector<size_t> pending{from};
        visited[from] = true;
        while (!pending.empty()) {
            const size_t current = pending.back();
            pending.pop_back();
            for (const size_t next : adjacency[current]) {
                if (next == to) {
                    return true;
                }
                if (!visited[next]) {
                    visited[next] = true;
                    pending.push_back(next);
                }
            }
        }
        return false;
    };

    // Every pair of writers targeting the same physical texture must have an
    // explicit or data-derived order. Otherwise WAW order would fall back to
    // addPass(), violating the declarative graph contract.
    for (const auto& [resource, writers] : writersByResource) {
        for (size_t lhs = 0; lhs < writers.size(); ++lhs) {
            for (size_t rhs = lhs + 1; rhs < writers.size(); ++rhs) {
                if (!hasPath(writers[lhs], writers[rhs]) &&
                    !hasPath(writers[rhs], writers[lhs])) {
                    addDiagnostic(
                        "Texture '" + resource +
                        "' has unordered writer passes '" +
                        passes[writers[lhs]]->getName() + "' and '" +
                        passes[writers[rhs]]->getName() +
                        "'; add a resource producer or execution dependency");
                }
            }
        }
    }

    // A reader of an intermediate version must complete before a later writer
    // reuses the same physical texture. These anti-dependencies preserve the
    // selected version without relying on declaration order.
    for (const auto& read : resolvedReads) {
        const auto writers = writersByResource.find(read.resource);
        if (writers == writersByResource.end()) {
            continue;
        }
        for (const size_t laterWriter : writers->second) {
            if (laterWriter == read.producer ||
                laterWriter == read.reader ||
                !hasPath(read.producer, laterWriter)) {
                continue;
            }
            if (hasPath(laterWriter, read.reader)) {
                addDiagnostic(
                    "Pass '" + passes[read.reader]->getName() +
                    "' requests texture '" + read.resource +
                    "' from producer '" + passes[read.producer]->getName() +
                    "', but a later writer '" +
                    passes[laterWriter]->getName() +
                    "' is ordered before the read");
                continue;
            }
            addEdge(
                read.reader,
                laterWriter,
                read.resource,
                RenderGraphHazard::WriteAfterRead);
        }
    }

    std::priority_queue<size_t, std::vector<size_t>, std::greater<>> ready;
    size_t nonNullPassCount = 0;
    for (size_t index = 0; index < passes.size(); ++index) {
        if (!passes[index]) {
            continue;
        }
        ++nonNullPassCount;
        if (indegree[index] == 0) {
            ready.push(index);
        }
    }

    std::vector<size_t> order;
    order.reserve(nonNullPassCount);
    while (!ready.empty()) {
        const size_t producer = ready.top();
        ready.pop();
        order.push_back(producer);
        for (const size_t consumer : adjacency[producer]) {
            if (--indegree[consumer] == 0) {
                ready.push(consumer);
            }
        }
    }
    if (order.size() != nonNullPassCount) {
        graph.valid_ = false;
        graph.diagnostics_.push_back(
            "Render Graph contains a resource dependency cycle");
        // An invalid graph has no executable fallback order. Returning the
        // declaration order here would silently make addPass() ordering part
        // of execution semantics and could execute unresolved hazards.
        order.clear();
    }

    std::vector<size_t> declarationToExecution(passes.size(), InvalidNode);
    graph.nodes_.reserve(order.size());
    for (size_t executionIndex = 0; executionIndex < order.size(); ++executionIndex) {
        declarationToExecution[order[executionIndex]] = executionIndex;
        graph.nodes_.push_back(std::move(declaredNodes[order[executionIndex]]));
    }
    for (auto& edge : graph.edges_) {
        edge.producer = declarationToExecution[edge.producer];
        edge.consumer = declarationToExecution[edge.consumer];
    }
    std::sort(
        graph.edges_.begin(), graph.edges_.end(),
        [](const RenderGraphEdge& lhs, const RenderGraphEdge& rhs) {
            if (lhs.consumer != rhs.consumer) return lhs.consumer < rhs.consumer;
            if (lhs.producer != rhs.producer) return lhs.producer < rhs.producer;
            if (lhs.resource != rhs.resource) return lhs.resource < rhs.resource;
            return lhs.hazard < rhs.hazard;
        });

    struct Range { size_t first = InvalidNode; size_t last = 0; };
    std::unordered_map<std::string, Range> ranges;
    std::unordered_set<std::string> crossFrameResources;
    for (size_t index = 0; index < graph.nodes_.size(); ++index) {
        const auto record = [&](const PipelineResourceRef& resource) {
            auto& range = ranges[resource.resource];
            range.first = std::min(range.first, index);
            range.last = std::max(range.last, index);
        };
        for (const auto& read : graph.nodes_[index].reads) {
            record(read);
            if (read.previousFrame) {
                crossFrameResources.insert(read.resource);
            }
        }
        for (const auto& write : graph.nodes_[index].writes) record(write);
    }
    for (const auto& [resource, range] : ranges) {
        graph.resourceLifetimes_.push_back({
            resource, range.first, range.last,
            externalResources.contains(resource),
            crossFrameResources.contains(resource),
            pipeline.getBuffer(resource) != nullptr});
    }
    std::sort(
        graph.resourceLifetimes_.begin(), graph.resourceLifetimes_.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.firstUse != rhs.firstUse) return lhs.firstUse < rhs.firstUse;
            return lhs.resource < rhs.resource;
        });

    if (!graph.valid_) {
        // getNodes() is the executable order. Invalid graphs retain their
        // diagnostics only and therefore cannot be consumed accidentally by
        // a later compilation or execution stage.
        graph.nodes_.clear();
        graph.edges_.clear();
        graph.resourceLifetimes_.clear();
    }

    return graph;
}

bool RenderGraph::isValid() const { return valid_; }
const std::vector<RenderGraphNode>& RenderGraph::getNodes() const { return nodes_; }
const std::vector<RenderGraphEdge>& RenderGraph::getEdges() const { return edges_; }
const std::vector<RenderGraphResourceLifetime>&
RenderGraph::getResourceLifetimes() const { return resourceLifetimes_; }
const std::vector<std::string>& RenderGraph::getDiagnostics() const {
    return diagnostics_;
}

const char* renderGraphHazardName(RenderGraphHazard hazard) {
    switch (hazard) {
    case RenderGraphHazard::ReadAfterWrite: return "RAW";
    case RenderGraphHazard::WriteAfterRead: return "WAR";
    case RenderGraphHazard::WriteAfterWrite: return "WAW";
    case RenderGraphHazard::Explicit: return "Explicit";
    }
    return "Unknown";
}

} // namespace Tasrovy::Render
