#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "PrimitiveSceneProxy.h"

namespace Tasrovy::Render {
class Object;
class PipelineBase;
class Scene;
}

namespace Tasrovy::Renderer {

// Thread-safe render-thread scene submission state. The game-facing Scene is
// cloned at the boundary today; this class is the future home for incremental
// primitive proxy updates without changing SceneRenderer's frame orchestration.
class RenderScene {
public:
    struct Snapshot {
        std::shared_ptr<Tasrovy::Render::Scene> scene;
        std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline;
        bool dirty = false;
        uint64_t version = 0;
    };

    class LockedState {
    public:
        std::shared_ptr<Tasrovy::Render::Scene>& scene();
        std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline();
        void markDirty();

    private:
        friend class RenderScene;
        explicit LockedState(RenderScene& owner);

        RenderScene& owner_;
        std::unique_lock<std::mutex> lock_;
    };

    void submitScene(std::shared_ptr<Tasrovy::Render::Scene> scene);
    void submitPipeline(
        std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline);
    void addPrimitive(const Tasrovy::Render::Object& object);
    void updatePrimitive(const Tasrovy::Render::Object& object);
    void removePrimitive(const std::string& name);

    Snapshot snapshot() const;
    LockedState lock();
    void acknowledge(uint64_t version);
    void adoptPipelineIfEmpty(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline);

private:
    void markDirtyLocked();
    void rebuildProxiesLocked();
    void applyProxyLocked(const PrimitiveSceneProxy& proxy);

    mutable std::mutex mutex_;
    std::shared_ptr<Tasrovy::Render::Scene> scene_;
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline_;
    bool dirty_ = true;
    uint64_t version_ = 0;
    std::unordered_map<uint64_t, PrimitiveSceneProxy> proxies_;
};

} // namespace Tasrovy::Renderer
