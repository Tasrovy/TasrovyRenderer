#pragma once

#include "RendererSettings.h"
#include "TSVector.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Tasrovy::Renderer {

class RenderScene;
struct SceneRendererComponents;

// Owns renderer inspection and runtime tuning UI. The main thread reads only
// published snapshots and enqueues settings; the render thread applies those
// commands at frame boundaries. Scene edits still use RenderScene's publication
// boundary and never access the render thread's active scene directly.
class RendererDebugUI {
public:
    RendererDebugUI(
        RenderScene& renderScene,
        SceneRendererComponents& components);

    void refreshExecutionSnapshot();
    void publishRuntimeSnapshot();
    void consumeUICommands();
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
    } executionSnapshotSource_;

    struct GraphEdgeSnapshot {
        std::string producer;
        std::string consumer;
        std::string resource;
        std::string hazard;
    };
    struct ResourceLifetimeSnapshot {
        std::string resource;
        size_t firstUse = 0;
        size_t lastUse = 0;
        bool external = false;
    };
    struct SkyboxSnapshot {
        std::string name;
        std::string path;
    };
    struct RuntimeSnapshot {
        RendererSettings settings;
        ExecutionSnapshot execution;
        size_t deferredDeletionCount = 0;
        std::vector<std::pair<std::string, double>> gpuPassTimings;
        uint64_t meshBufferBytes = 0;
        uint64_t skyboxBufferBytes = 0;
        size_t meshCount = 0;
        size_t materialTextureCount = 0;
        bool renderGraphValid = false;
        std::vector<std::string> framePassNames;
        size_t frameDrawCount = 0;
        std::vector<GraphEdgeSnapshot> graphEdges;
        std::vector<ResourceLifetimeSnapshot> resourceLifetimes;
        std::vector<std::string> graphDiagnostics;
        size_t executionPlanPassCount = 0;
        size_t executionPlanResourceCount = 0;
        size_t executionPlanDiagnosticCount = 0;
        uint32_t internalRenderWidth = 0;
        uint32_t internalRenderHeight = 0;
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
        std::string historyStatus;
        uint64_t temporalFrameIndex = 0;
        Tasrovy::Base::TSVec2f previousJitterUv =
            Tasrovy::Base::TSVec2f(0.0f);
        std::vector<SkyboxSnapshot> skyboxes;
        int selectedSkyboxIndex = 0;
        std::string activeSkyboxName;
    };
    struct UICommand {
        RendererSettings settings;
        bool resetTemporalHistory = false;
        bool internalExtentDirty = false;
        std::optional<int> selectedSkyboxIndex;
    };

    RuntimeSnapshot readRuntimeSnapshot() const;
    void submitUICommand(UICommand command);

    mutable std::mutex snapshotMutex_;
    std::array<RuntimeSnapshot, 2> runtimeSnapshots_;
    uint32_t publishedSnapshotIndex_ = 0;
    mutable std::mutex commandMutex_;
    std::optional<UICommand> pendingCommand_;
    RendererSettings uiSettings_;
    bool uiSettingsInitialized_ = false;

    bool taffyRotationEnabled_ = false;
    Tasrovy::Base::TSVec3f taffyBaseRotation_ =
        Tasrovy::Base::TSVec3f(0.0f);
    float taffyYawOffset_ = 0.0f;
    std::chrono::steady_clock::time_point lastTaffyRotationTime_{};

    RenderScene& renderScene_;
    SceneRendererComponents& components_;
};

} // namespace Tasrovy::Renderer
