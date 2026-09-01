#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Tasrovy::Renderer {

class RenderScene;
struct SceneRendererComponents;

// Owns renderer inspection and runtime tuning UI. It may edit the published
// scene through RenderScene, but it does not compile or execute frames.
class RendererDebugUI {
public:
    RendererDebugUI(
        RenderScene& renderScene,
        SceneRendererComponents& components);

    void refreshExecutionSnapshot();
    void draw();

private:
    struct AttachmentSnapshot {
        std::string name;
        bool depth = false;
    };
    struct PassSnapshot {
        std::string name;
        size_t objectCount = 0;
        bool usesSwapchain = false;
        std::vector<AttachmentSnapshot> attachments;
    };
    struct ExecutionSnapshot {
        size_t passCount = 0;
        size_t textureCount = 0;
        uint64_t allocatedBytes = 0;
        uint64_t uniformPerFrameBytes = 0;
        uint64_t uniformResidentBytes = 0;
        std::vector<PassSnapshot> passes;
    } executionSnapshot_;

    RenderScene& renderScene_;
    SceneRendererComponents& components_;
};

} // namespace Tasrovy::Renderer
