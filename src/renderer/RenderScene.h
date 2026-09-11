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

struct SceneVersions {
    uint64_t structural = 0;
    uint64_t transform = 0;
    uint64_t material = 0;
    uint64_t lighting = 0;
    uint64_t pipeline = 0;

    bool operator==(const SceneVersions&) const = default;
};

enum class SceneChange : uint32_t {
    None = 0,
    Structural = 1u << 0u,
    Transform = 1u << 1u,
    Material = 1u << 2u,
    Lighting = 1u << 3u,
    Pipeline = 1u << 4u,
    AllScene = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u)
};

constexpr SceneChange operator|(SceneChange lhs, SceneChange rhs) {
    return static_cast<SceneChange>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr SceneChange& operator|=(SceneChange& lhs, SceneChange rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasSceneChange(SceneChange changes, SceneChange change) {
    return (static_cast<uint32_t>(changes) & static_cast<uint32_t>(change)) != 0;
}

// Thread-safe publication boundary. Writers edit scene_, while readers only
// receive an immutable clone whose lifetime is independent from later edits.
class RenderScene {
public:
    struct Snapshot {
        std::shared_ptr<const Tasrovy::Render::Scene> scene;
        std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline;
        SceneVersions versions;
    };

    class LockedState {
    public:
        std::shared_ptr<Tasrovy::Render::Scene>& scene();
        std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline();
        void markChanged(SceneChange changes);

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
    void adoptPipelineIfEmpty(
        const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline);

private:
    void markChangedLocked(SceneChange changes);
    void publishSceneLocked();
    void rebuildProxiesLocked();
    void applyProxyLocked(const PrimitiveSceneProxy& proxy);

    mutable std::mutex mutex_;
    std::shared_ptr<Tasrovy::Render::Scene> scene_;
    std::shared_ptr<const Tasrovy::Render::Scene> publishedScene_;
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline_;
    SceneVersions versions_;
    std::unordered_map<uint64_t, PrimitiveSceneProxy> proxies_;
};

} // namespace Tasrovy::Renderer
