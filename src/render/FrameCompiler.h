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

struct FrameObjectSource {
    std::weak_ptr<Object> object;
    std::shared_ptr<Material> material;
    uint32_t submeshIndex = 0;
};

struct FrameSourceRegistry {
    std::unordered_map<RenderObjectId, std::weak_ptr<Object>> objects;
    std::unordered_map<RenderMeshId, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<RenderMaterialId, std::shared_ptr<Material>> materials;
    std::unordered_map<RenderMaterialId, uint32_t> materialIndices;
    std::unordered_map<uint32_t, FrameObjectSource> objectDataSources;

    void clear() {
        objects.clear();
        meshes.clear();
        materials.clear();
        materialIndices.clear();
        objectDataSources.clear();
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
    uint32_t objectIndexFor(RenderObjectId objectId, uint32_t submeshIndex);
    uint32_t materialIndexFor(RenderMaterialId materialId);

    uint64_t nextId_ = 1;
    std::unordered_map<const void*, uint64_t> objectIds_;
    uint32_t nextObjectIndex_ = 0;
    uint32_t nextMaterialIndex_ = 0;
    std::unordered_map<RenderObjectId,
        std::unordered_map<uint32_t, uint32_t>> objectIndices_;
    std::unordered_map<RenderMaterialId, uint32_t> materialIndices_;
    FrameSourceRegistry sourceRegistry_;
};

} // namespace Tasrovy::Render
