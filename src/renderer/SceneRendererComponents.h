#pragma once

#include "FrameExecutionScheduler.h"
#include "FrameOrchestrator.h"
#include "GpuDrivenGBufferSystem.h"
#include "GPUScene.h"
#include "RendererRHIContext.h"
#include "RendererSettings.h"
#include "SceneGPUResources.h"
#include "ShadowViewSystem.h"
#include "ViewState.h"
#include "ViewSystem.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Tasrovy::Windowing {
class Window;
}

namespace Tasrovy::Render {
class Object;
class PipelineBase;
}

namespace Tasrovy::UI {
class UIOverlay;
}

namespace Tasrovy::Renderer {

class ResourceMonitor;

// Runtime-owned subsystem collection. This type contains component lifetimes
// and shared frame state only; it does not compile graphs or record commands.
struct SceneRendererComponents {
    SceneRendererComponents(
        Tasrovy::Windowing::Window& window,
        uint32_t maxFrames);
    ~SceneRendererComponents();

    RendererRHIContext rhi;
    RendererSettings settings;
    ViewState viewState;
    ViewSystem viewSystem;
    SceneGPUResources sceneResources;
    GPUScene gpuScene;
    FrameOrchestrator frameOrchestrator;
    FrameExecutionScheduler frameExecutionScheduler;
    ShadowViewSystem shadowViewSystem;
    std::shared_ptr<Tasrovy::Render::PipelineBase> compiledPipeline;
    uint64_t compiledPipelineConfigurationVersion = 0;
    std::string lastFramePlanDiagnostics;
    bool environmentLightingEnabled = false;
    bool loggedSkyboxDrawState = false;
    bool loggedSubmeshMaterialBindings = false;
    GpuDrivenGBufferSystem gpuDrivenGBuffer;
    std::vector<std::vector<std::string>> gpuTimingNamesPerFrame;
    std::vector<std::pair<std::string, double>> gpuPassTimings;
    std::unique_ptr<Tasrovy::UI::UIOverlay> ui;
    std::unique_ptr<ResourceMonitor> resourceMonitor;
    uint32_t maxFramesInFlight = 0;
    // Render-thread-owned copy of the last successfully created display
    // extent. It lets frame N+1 be prepared without reading the RHI-owned
    // swapchain scheduler while frame N is being recorded/submitted.
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;
    uint32_t internalRenderWidth = 0;
    uint32_t internalRenderHeight = 0;
    bool internalExtentDirty = false;
};

} // namespace Tasrovy::Renderer
