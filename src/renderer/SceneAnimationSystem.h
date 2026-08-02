#pragma once

#include <chrono>

namespace Tasrovy::Render {
class Scene;
}

namespace Tasrovy::Renderer {
struct SceneRendererComponents;

// Optional scene animation used by temporal-stability demonstrations. It is
// isolated from frame compilation and GPU execution.
class SceneAnimationSystem {
public:
    static void update(
        Tasrovy::Render::Scene& scene,
        SceneRendererComponents& state,
        std::chrono::steady_clock::time_point now);
};

} // namespace Tasrovy::Renderer
