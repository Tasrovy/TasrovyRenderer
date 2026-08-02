#pragma once

#include "FramePacket.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Tasrovy::Render {

class Object;
class Mesh;
class Material;
class PipelineBase;
class RenderGraph;
class Scene;

struct FrameSourceRegistry {
    std::unordered_map<RenderObjectId, std::weak_ptr<Object>> objects;
    std::unordered_map<RenderMeshId, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<RenderMaterialId, std::shared_ptr<Material>> materials;

    void clear() {
        objects.clear();
        meshes.clear();
        materials.clear();
    }
};

class FrameCompiler {
public:
    FramePacket compile(
        const Scene& scene,
        const PipelineBase& pipeline,
        const RenderGraph& renderGraph,
        uint64_t frameNumber);

    void resetHistory();
    const FrameSourceRegistry& sourceRegistry() const { return sourceRegistry_; }

private:
    uint64_t idFor(const void* object);

    uint64_t nextId_ = 1;
    std::unordered_map<const void*, uint64_t> objectIds_;
    std::unordered_map<const Object*, TSMat4f> previousModels_;
    FrameSourceRegistry sourceRegistry_;
};

} // namespace Tasrovy::Render
