#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace Tasrovy::Windowing { class Window; }

namespace Tasrovy {
namespace Render {
class Scene;
class PipelineBase;
}
}

namespace Tasrovy::RHI {

class SceneRenderer {
public:
    explicit SceneRenderer(Tasrovy::Windowing::Window& window, uint32_t maxFramesInFlight = 2);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void setScene(std::shared_ptr<Tasrovy::Render::Scene> scene);
    void setPipeline(std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline);

    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    void drawSceneDebugUI();
    void renderLoop();
    void processScene(const std::shared_ptr<Tasrovy::Render::Scene>& scene);
    void renderFrame(Tasrovy::Render::Scene& scene);
    void prepareSkyboxVariants(const std::string& preferredPath);

    Tasrovy::Windowing::Window& window_;
    uint32_t maxFramesInFlight_;

    struct RenderState;
    std::unique_ptr<RenderState> renderState_;

    std::shared_ptr<Tasrovy::Render::Scene> currentScene_;
    std::shared_ptr<Tasrovy::Render::PipelineBase> currentPipeline_;
    std::mutex sceneMutex_;
    bool renderDataDirty_ = true;
    uint64_t renderDataDirtyVersion_ = 0;

    std::thread renderThread_;
    std::atomic<bool> running_{false};
};

} // namespace Tasrovy::RHI
