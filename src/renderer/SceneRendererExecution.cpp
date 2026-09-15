#include "SceneRendererExecution.h"
#include "RendererDebugUI.h"
#include "RenderFrameSubmission.h"

#include "../RHI/CompiledRenderPipeline.h"
#include "FrameParameterBuilder.h"
#include "FrameBindingResolver.h"
#include "FrameRuntimeParameterCompiler.h"
#include "FrameExecutionScheduler.h"
#include "GpuDrivenGBufferSystem.h"
#include "RendererSettings.h"
#include "RendererFeaturePolicy.h"
#include "FrameOrchestrator.h"
#include "RendererRHIContext.h"
#include "SceneGPUResources.h"
#include "ShadowViewSystem.h"
#include "ViewState.h"
#include "ViewSystem.h"
#include "../RHI/Buffer.h"
#include "../RHI/CommandList.h"
#include "../RHI/Device.h"
#include "../RHI/FrameScheduler.h"
#include "../RHI/Descriptor.h"
#include "../RHI/FrameExecutor.h"
#include "../RHI/Image.h"
#include "../RHI/Pass.h"
#include "../RHI/Pipeline.h"
#include "ResourceMonitor.h"
#include "../RHI/RenderFramePlan.h"
#include "../RHI/ResourceTracker.h"
#include "SkyboxGeometry.h"
#include "../render/FrameCompiler.h"
#include "../render/FramePacket.h"
#include "../render/Material.h"
#include "../render/MaterialDescriptor.h"
#include "../render/PBRMaterialBindings.h"
#include "../render/Camera.h"
#include "../render/DeferredPipeline.h"
#include "../render/Light.h"
#include "../render/Mesh.h"
#include "../render/Object.h"
#include "../render/PBRPipeline.h"
#include "../render/Pipeline.h"
#include "../render/PipelinePass.h"
#include "../render/RenderGraph.h"
#include "../render/Scene.h"
#include "../render/Shader.h"
#include "../render/Texture.hpp"
#include "../ui/UI.h"
#include "../window/Window.h"
#include "Logger.hpp"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Tasrovy::Renderer {

using namespace Tasrovy::Render;
using namespace Tasrovy::RHI;

namespace {

using PassResources = FramePassPacket;
inline constexpr const char* OutlineOnlyDebugOutput = "__OutlineOnly";

enum class RHIFrameStage : uint8_t {
    Queued,
    FenceAndAcquire,
    Recording,
    SubmitAndPresent,
    Complete
};

const char* rhiFrameStageName(RHIFrameStage stage) {
    switch (stage) {
    case RHIFrameStage::Queued: return "Queued";
    case RHIFrameStage::FenceAndAcquire: return "FenceAcquire";
    case RHIFrameStage::Recording: return "Recording";
    case RHIFrameStage::SubmitAndPresent: return "SubmitPresent";
    case RHIFrameStage::Complete: return "Complete";
    }
    return "Unknown";
}

uint32_t makeEvenExtent(float value) {
    const uint32_t rounded = std::max(2u, static_cast<uint32_t>(std::lround(value)));
    return (rounded + 1u) & ~1u;
}

std::pair<uint32_t, uint32_t> calculateInternalExtent(
    uint32_t displayWidth,
    uint32_t displayHeight,
    float resolutionPercent) {
    const float scale = std::max(resolutionPercent, 1.0f) * 0.01f;
    return {
        makeEvenExtent(static_cast<float>(std::max(displayWidth, 1u)) * scale),
        makeEvenExtent(static_cast<float>(std::max(displayHeight, 1u)) * scale)
    };
}

void collectSceneObjects(
    const std::shared_ptr<Object>& object,
    std::vector<std::shared_ptr<Object>>& objects,
    std::unordered_set<const Object*>& visited) {
    if (!object || !object->isActive() || !visited.insert(object.get()).second) {
        return;
    }

    objects.push_back(object);
    for (const auto& child : object->getChildren()) {
        collectSceneObjects(child, objects, visited);
    }
}


} // namespace

struct SceneRendererExecution::PendingRHIFrame {
    struct Result {
        bool submitted = false;
        bool swapchainRebuildRequired = false;
        std::vector<std::pair<std::string, double>> completedTimings;
        std::atomic<RHIFrameStage> stage{RHIFrameStage::Queued};
    };

    std::future<void> completion;
    std::shared_ptr<Result> result;
    uint64_t frameNumber = 0;
    uint32_t frameIndex = 0;
};

SceneRendererExecution::SceneRendererExecution(
    Tasrovy::Windowing::Window& window,
    uint32_t maxFramesInFlight,
    SceneRendererComponents& components,
    RenderScene& renderScene,
    RenderThread& renderThread,
    RHIThread& rhiThread)
    : window_(window),
      maxFramesInFlight_(maxFramesInFlight),
      renderState_(&components),
      renderThread_(renderThread),
      rhiThread_(rhiThread),
      sceneUpdates_(renderScene),
      debugUI_(std::make_unique<RendererDebugUI>(renderScene, components)) {
    if (renderState_->ui) {
        renderState_->ui->setDrawCallback([this]() {
            debugUI_->draw();
        });
    }
    debugUI_->refreshExecutionSnapshot();
    debugUI_->publishRuntimeSnapshot();
    LOG_INFO("SceneRenderer: RHI initialized");
}

SceneRendererExecution::~SceneRendererExecution() {
    try {
        waitForPendingRHIFrame();
    } catch (const std::exception& error) {
        LOG_ERROR("SceneRenderer: RHI worker shutdown failed: {}", error.what());
    }
}

void SceneRendererExecution::buildUIFrame() {
    const auto framebuffer = window_.getFramebufferState();
    if (framebuffer.width <= 0 || framebuffer.height <= 0 ||
        !renderState_->ui) {
        return;
    }
    // GLFW event callbacks and ImGui frame construction both run on the main
    // thread. The render/RHI threads only consume the captured immutable draw
    // data identified by the published token.
    renderState_->ui->beginFrame(
        static_cast<uint32_t>(framebuffer.width),
        static_cast<uint32_t>(framebuffer.height));
}

bool SceneRendererExecution::consumeOldestRHIFrame(bool wait) {
    if (pendingRHIFrames_.empty()) return true;
    auto& front = pendingRHIFrames_.front();
    if (!wait && front->completion.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
        return true;
    }
    auto pending = std::move(front);
    pendingRHIFrames_.pop_front();
    pending->completion.get();
    if (!pending->result) return true;

    renderState_->gpuPassTimings =
        std::move(pending->result->completedTimings);
    pendingSwapchainRebuild_ = pendingSwapchainRebuild_ ||
        pending->result->swapchainRebuildRequired;
    if (!pending->result->submitted) {
        renderState_->viewState.invalidate(
            "RHI frame was not submitted", true);
        renderState_->frameOrchestrator.resetTemporalHistory();
    }
    return pending->result->submitted;
}

bool SceneRendererExecution::waitForPendingRHIFrame() {
    bool submitted = true;
    if (!renderThread_.running() && !pendingRHIFrames_.empty()) {
        LOG_INFO(
            "Shutdown: RenderThread draining {} accepted RHI frame(s)",
            pendingRHIFrames_.size());
    }
    while (!pendingRHIFrames_.empty()) {
        if (!renderThread_.running()) {
            const auto& pending = pendingRHIFrames_.front();
            LOG_INFO(
                "Shutdown: waiting for RHI frame {}, slot {}, stage={}",
                pending->frameNumber, pending->frameIndex,
                rhiFrameStageName(pending->result->stage.load(
                    std::memory_order_acquire)));
        }
        submitted = consumeOldestRHIFrame(true) && submitted;
    }
    if (!renderThread_.running()) {
        LOG_INFO("Shutdown: all accepted RHI frames completed");
    }
    return submitted;
}

bool SceneRendererExecution::pollPendingRHIFrame() {
    bool submitted = true;
    while (!pendingRHIFrames_.empty() &&
           pendingRHIFrames_.front()->completion.wait_for(
               std::chrono::seconds(0)) == std::future_status::ready) {
        submitted = consumeOldestRHIFrame(false) && submitted;
    }
    if (!submitted) {
        // Later speculative frames depend on the failed frame-slot sequence.
        // Drain them before allowing swapchain recovery or graph rebuilding.
        waitForPendingRHIFrame();
    }
    return submitted;
}

void SceneRendererExecution::run() {
    try {
        renderLoop();
        LOG_INFO("Shutdown: RenderThread production loop stopped");
        waitForPendingRHIFrame();
    } catch (const std::exception& error) {
        LOG_ERROR("SceneRenderer: render/RHI pipeline stopped: {}", error.what());
    }
}

void SceneRendererExecution::renderLoop() {
    uint64_t appliedResizeGeneration = 0;
    while (renderThread_.running()) {
        // UI writes are coalesced on the main thread and applied only here,
        // at a render-frame boundary. No UI lock is held while recording,
        // waiting for the RHI worker, or presenting.
        debugUI_->consumeUICommands();
        // Consume a completed RHI result without stalling. If frame N is still
        // recording/submitting, CPU preparation of N+1 continues below.
        if (!pollPendingRHIFrame()) continue;
        if (!renderThread_.running()) break;
        const auto update = sceneUpdates_.synchronize(
            [this](const auto& pipeline) {
                if (pipeline) {
                    pipeline->applyConfiguration(
                        RendererFeaturePolicy::configuration(
                            renderState_->settings));
                }
                return renderState_->compiledPipeline &&
                    renderState_->compiledPipeline->getConfigurationVersion() !=
                        renderState_->compiledPipelineConfigurationVersion;
            });
        const auto scene = update.scene;
        if (!scene) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (update.rebuildRequired) {
            rebuildRenderGraph(scene);
        }

        const auto framebuffer = window_.getFramebufferState();
        if (framebuffer.width <= 0 || framebuffer.height <= 0) {
            // GLFW event processing stays on the main thread. A minimized
            // surface has no valid extent, so pause rendering until the next
            // framebuffer callback publishes a usable size.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        const bool windowSizeChanged =
            framebuffer.resizeGeneration != appliedResizeGeneration;
        auto& frameScheduler = renderState_->rhi.device->getFrameScheduler();
        bool swapchainRecreated = false;
        // The rebuild flag belongs to the RHI scheduler. Do not read it while
        // an RHI frame may be changing it; an out-of-date acquire is consumed
        // through PendingRHIFrame and handled on the next iteration.
        const bool swapchainRebuildRequired = pendingSwapchainRebuild_ ||
            (pendingRHIFrames_.empty() &&
             frameScheduler.isSwapchainRebuildRequired());
        if (windowSizeChanged || swapchainRebuildRequired) {
            waitForPendingRHIFrame();
            bool recreated = false;
            rhiThread_.invoke([&] {
                recreated = frameScheduler.recreateSwapchain(
                    static_cast<uint32_t>(framebuffer.width),
                    static_cast<uint32_t>(framebuffer.height));
                if (recreated) rhiFrameSequenceValid_ = true;
            });
            if (!recreated) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            appliedResizeGeneration = framebuffer.resizeGeneration;
            swapchainRecreated = true;
            pendingSwapchainRebuild_ = false;
            renderState_->displayWidth = frameScheduler.getWidth();
            renderState_->displayHeight = frameScheduler.getHeight();
            if (auto* cam = scene->getPrimaryCamera()) {
                cam->setAspect(
                    static_cast<float>(renderState_->displayWidth) /
                    static_cast<float>(renderState_->displayHeight));
            }
        }

        const auto desiredInternalExtent = renderState_->settings.temporalAAMode == 1
            ? std::pair<uint32_t, uint32_t>{
                renderState_->displayWidth, renderState_->displayHeight}
            : calculateInternalExtent(
                renderState_->displayWidth,
                renderState_->displayHeight,
                renderState_->settings.internalResolutionPercent);
        const bool internalExtentChanged =
            desiredInternalExtent.first != renderState_->internalRenderWidth ||
            desiredInternalExtent.second != renderState_->internalRenderHeight;
        if (renderState_->internalExtentDirty || internalExtentChanged) {
            renderState_->internalRenderWidth = desiredInternalExtent.first;
            renderState_->internalRenderHeight = desiredInternalExtent.second;
            renderState_->internalExtentDirty = false;
            renderState_->viewState.temporalHistoryValid = false;
            rebuildRenderGraph(scene);
        } else if (swapchainRecreated) {
            const auto pipeline = sceneUpdates_.currentPipeline();
            if (pipeline) {
                rebuildDisplayResources(*pipeline);
            } else {
                rebuildRenderGraph(scene);
            }
        }

        renderFrame(*scene);
        debugUI_->publishRuntimeSnapshot();
    }
}

void SceneRendererExecution::applySceneUpdates(
    const std::shared_ptr<Scene>& scene) {
    if (!scene) return;
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();

    std::vector<std::shared_ptr<Object>> objects;
    std::unordered_set<const Object*> visited;
    for (const auto& object : scene->getObjects()) {
        collectSceneObjects(object, objects, visited);
    }
    state.sceneResources.rebuildMeshes(
        device, frameScheduler, *state.rhi.commandList,
        state.rhi.sceneResourceScope, objects);

    const auto& sources = state.frameOrchestrator.sourceRegistry();
    for (const auto& pass : state.frameOrchestrator.framePacket().passes) {
        for (const auto& requirement : pass.materialTextures) {
            state.sceneResources.ensureDefaultTexture(
                device, state.rhi.sceneResourceScope, requirement);
        }
        for (const auto objectId : pass.objectIds) {
            const auto foundObject = sources.objects.find(objectId);
            const auto object = foundObject == sources.objects.end()
                ? nullptr : foundObject->second.lock();
            if (!object) continue;
            state.sceneResources.ensureMaterialTextures(
                device, state.rhi.sceneResourceScope,
                object->getMaterial(), pass.materialTextures);
            if (const auto mesh = object->getMesh()) {
                for (const auto& submesh : mesh->getSubmeshes()) {
                    state.sceneResources.ensureMaterialTextures(
                        device, state.rhi.sceneResourceScope,
                        submesh.getMaterial(), pass.materialTextures);
                }
            }
        }
    }

    state.environmentLightingEnabled = false;
    state.sceneResources.prepareGlobalTextures(
        device, state.rhi.persistentResourceScope);
    // Skybox rendering is currently disabled. Keep only the tiny neutral IBL
    // resources required by the fixed descriptor layout; do not discover,
    // upload, or retain cubemap variants and do not create skybox geometry.
    state.sceneResources.prepareEnvironmentFallbacks(
        device, state.rhi.persistentResourceScope);
    state.sceneResources.rebuildSkyboxGeometry(
        device, frameScheduler, *state.rhi.commandList,
        state.rhi.sceneResourceScope, false);
}

void SceneRendererExecution::rebuildRenderGraph(
    const std::shared_ptr<Scene>& scene) {
    if (!scene) return;
    if (!rhiThread_.isCurrentThread()) {
        waitForPendingRHIFrame();
        rhiThread_.invoke([this, scene] { rebuildRenderGraph(scene); });
        debugUI_->refreshExecutionSnapshot();
        return;
    }
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();
    auto pipeline = sceneUpdates_.currentPipeline();

    const auto beforeRebuild = RHI::ResourceTracker::snapshot();
    LOG_GPU_MEMORY(
        "[REBUILD_BEGIN] scope=RenderGraph totalLiveResources={} "
        "totalLiveBytes={} deferredDeletes={}",
        beforeRebuild.totalLiveCount,
        beforeRebuild.totalLiveBytes,
        device.getDeferredDeletionCount());

    if (!pipeline) {
        pipeline = DeferredPipeline::create();
        sceneUpdates_.adoptPipelineIfEmpty(pipeline);
    }

    pipeline->applyConfiguration(
        RendererFeaturePolicy::configuration(state.settings));

    state.frameOrchestrator.rebuild(scene, *pipeline, 0);
    if (!state.settings.debugOutputResource.empty() &&
        state.settings.debugOutputResource != OutlineOnlyDebugOutput) {
        const bool debugResourceStillExists = std::any_of(
            pipeline->getTextures().begin(),
            pipeline->getTextures().end(),
            [&](const PipelineTextureDesc& texture) {
                return texture.name == state.settings.debugOutputResource;
            });
        if (!debugResourceStillExists) {
            state.settings.debugOutputResource.clear();
            state.settings.debugOutputSemantic =
                DebugTextureSemantic::FinalOutput;
        }
    }
    const auto& renderGraph =
        state.frameOrchestrator.renderGraph();
    for (const auto& diagnostic : renderGraph.getDiagnostics()) {
        LOG_WARN("SceneRenderer: Render Graph issue: {}", diagnostic);
    }
    LOG_INFO(
        "SceneRenderer: compiled Render Graph '{}' with {} nodes, {} edges and {} resources",
        pipeline->getName(),
        renderGraph.getNodes().size(),
        renderGraph.getEdges().size(),
        renderGraph.getResourceLifetimes().size());

    // Rebuilding the render graph releases vertex/index buffers, descriptor
    // sets, pipelines, and render targets owned by the previous graph. Frames
    // submitted before a pipeline/scene switch may still reference them, so
    // complete all in-flight work before destroying those resources.
    frameScheduler.waitForInFlightFrames();
    if (state.rhi.externalFeatureExecutor) {
        if (state.settings.dlssNeuralRenderingEnabled) {
            state.rhi.externalFeatureExecutor->prepareForExtent(
                frameScheduler.getWidth(), frameScheduler.getHeight());
        } else {
            // Disabling the pass means there will be no later evaluation at
            // which the SDK-owned feature could release itself.
            state.rhi.externalFeatureExecutor->invalidateResources();
        }
    }

    state.rhi.frameExecutor.reset();
    state.sceneResources.resetScene();
    state.gpuScene.reset();
    state.rhi.frameExecutor.compiledPipeline().reset();
    state.loggedSubmeshMaterialBindings = false;
    state.viewState.invalidate("Render graph rebuilt", true);
    state.lastFramePlanDiagnostics.clear();
    state.gpuDrivenGBuffer.reset();
    for (auto& timingNames : state.gpuTimingNamesPerFrame) {
        timingNames.clear();
    }
    state.gpuPassTimings.clear();
    device.resetResourceScope(state.rhi.displayResourceScope);
    device.resetResourceScope(state.rhi.sceneResourceScope);

    // All in-flight frames were completed above. Clearing the owners only
    // queued backend destruction, so collect it now before the replacement
    // graph starts allocating images and buffers. This avoids holding the old
    // and new graph resource sets at the same time during a rebuild.
    const size_t retiredResourceCount = device.getDeferredDeletionCount();
    if (retiredResourceCount != 0) {
        frameScheduler.waitForInFlightFrames();
        LOG_INFO(
            "SceneRenderer: released {} retired GPU resources before "
            "allocating the rebuilt render graph",
            retiredResourceCount);
    }
    const auto afterRelease = RHI::ResourceTracker::snapshot();
    LOG_GPU_MEMORY(
        "[REBUILD_RELEASED] scope=RenderGraph totalLiveResources={} "
        "totalLiveBytes={} deferredDeletes={}",
        afterRelease.totalLiveCount,
        afterRelease.totalLiveBytes,
        device.getDeferredDeletionCount());

    auto& framePacket = state.frameOrchestrator.framePacket();
    const auto& executionPlan = state.frameOrchestrator.executionPlan();
    if (!renderGraph.isValid() ||
        !framePacket.valid() ||
        !executionPlan.valid()) {
        state.compiledPipeline.reset();
        state.compiledPipelineConfigurationVersion = 0;
        LOG_ERROR(
            "SceneRenderer: rejected invalid Render Graph '{}'; no GPU work will be compiled or executed",
            pipeline->getName());
        for (const auto& diagnostic : executionPlan.diagnostics) {
            LOG_ERROR("SceneRenderer: {}", diagnostic);
        }
        return;
    }

    state.compiledPipeline = pipeline;
    state.compiledPipelineConfigurationVersion =
        pipeline->getConfigurationVersion();

    applySceneUpdates(scene);

    const auto executionConfig = FrameResourceConfig{
        frameScheduler.getWidth(),
        frameScheduler.getHeight(),
        state.internalRenderWidth,
        state.internalRenderHeight,
        maxFramesInFlight_,
        VirtualShadowPageResolution,
        static_cast<uint32_t>(ShadowCascadeCount),
        state.rhi.sceneResourceScope,
        state.rhi.displayResourceScope
    };
    state.rhi.frameExecutor.compileExecution(
        device,
        framePacket,
        executionPlan,
        executionConfig);
    state.rhi.frameExecutor.bindFramePacket(
        framePacket);
    state.gpuScene.prepare(
        device,
        state.rhi.sceneResourceScope,
        maxFramesInFlight_,
        state.frameOrchestrator.sourceRegistry());
    // GPU-driven submission must be represented as explicit FramePacket
    // passes; the legacy side-channel compiler is intentionally not rebuilt.
    state.gpuDrivenGBuffer.reset();

    const auto afterRebuild = RHI::ResourceTracker::snapshot();
    LOG_GPU_MEMORY(
        "[REBUILD_END] scope=RenderGraph totalLiveResources={} "
        "totalLiveBytes={} deltaBytes={}",
        afterRebuild.totalLiveCount,
        afterRebuild.totalLiveBytes,
        static_cast<int64_t>(afterRebuild.totalLiveBytes) -
            static_cast<int64_t>(afterRelease.totalLiveBytes));

    LOG_INFO(
        "SceneRenderer: parsed '{}' into {} render textures, {} meshes, {} passes",
        pipeline->getName(),
        state.rhi.frameExecutor.textures().size(),
        state.sceneResources.meshCount(),
        state.rhi.frameExecutor.compiledPipeline().size());
}

void SceneRendererExecution::rebuildDisplayResources(PipelineBase& pipeline) {
    if (!rhiThread_.isCurrentThread()) {
        waitForPendingRHIFrame();
        rhiThread_.invoke(
            [this, &pipeline] { rebuildDisplayResources(pipeline); });
        debugUI_->refreshExecutionSnapshot();
        return;
    }
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();
    const auto beforeRebuild = RHI::ResourceTracker::snapshot();
    LOG_GPU_MEMORY(
        "[REBUILD_BEGIN] scope=Display totalLiveResources={} "
        "totalLiveBytes={} deferredDeletes={}",
        beforeRebuild.totalLiveCount,
        beforeRebuild.totalLiveBytes,
        device.getDeferredDeletionCount());

    // Display history and swapchain-facing pass descriptions depend on the
    // window extent. Internal GBuffer resources remain alive when the aspect
    // ratio (and therefore the fixed-height internal extent) is unchanged.
    frameScheduler.waitForInFlightFrames();
    if (state.rhi.externalFeatureExecutor) {
        if (state.settings.dlssNeuralRenderingEnabled) {
            state.rhi.externalFeatureExecutor->prepareForExtent(
                frameScheduler.getWidth(), frameScheduler.getHeight());
        } else {
            state.rhi.externalFeatureExecutor->invalidateResources();
        }
    }
    (void)pipeline;
    device.resetResourceScope(state.rhi.displayResourceScope);

    state.rhi.frameExecutor.rebuildDisplayResources(
        device,
        state.frameOrchestrator.executionPlan(),
        {
            frameScheduler.getWidth(),
            frameScheduler.getHeight(),
            state.internalRenderWidth,
            state.internalRenderHeight,
            maxFramesInFlight_,
            VirtualShadowPageResolution,
            static_cast<uint32_t>(ShadowCascadeCount),
            state.rhi.sceneResourceScope,
            state.rhi.displayResourceScope
        });

    state.viewState.temporalHistoryValid = false;
    state.viewState.temporalFrameIndex = 0;
    state.frameOrchestrator.resetTemporalHistory();
    const auto afterRebuild = RHI::ResourceTracker::snapshot();
    LOG_GPU_MEMORY(
        "[REBUILD_END] scope=Display totalLiveResources={} totalLiveBytes={} "
        "deltaBytes={} deferredDeletes={}",
        afterRebuild.totalLiveCount,
        afterRebuild.totalLiveBytes,
        static_cast<int64_t>(afterRebuild.totalLiveBytes) -
            static_cast<int64_t>(beforeRebuild.totalLiveBytes),
        device.getDeferredDeletionCount());
    LOG_INFO(
        "SceneRenderer: rebuilt display resources {}x{}; internal GBuffer remains {}x{}",
        frameScheduler.getWidth(),
        frameScheduler.getHeight(),
        state.internalRenderWidth,
        state.internalRenderHeight);
}

void SceneRendererExecution::renderFrame(Scene& scene) {
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();

    if (!renderThread_.running() || !scene.getPrimaryCamera() ||
        !state.compiledPipeline) {
        return;
    }

    const size_t frameWindow = std::max<size_t>(maxFramesInFlight_, 1u);
    if (pendingRHIFrames_.size() >= frameWindow &&
        !consumeOldestRHIFrame(true)) {
        waitForPendingRHIFrame();
        return;
    }
    if (pendingRHIFrames_.empty()) {
        nextRHIFrameIndex_ = frameScheduler.getCurrentFrameIndex();
    }
    // The queue position predicts the slot RHI will acquire. CPU preparation
    // does not write mapped per-frame buffers; upload happens after that slot's
    // fence is waited on the consumer thread.
    const uint32_t frameIdx = nextRHIFrameIndex_;
    const uint32_t displayWidth = state.displayWidth;
    const uint32_t displayHeight = state.displayHeight;
    std::unordered_map<const Object*, TSMat4f> currentModelMatrices;
    currentModelMatrices.reserve(state.viewState.previousModelMatrices.size() + 4u);

    auto* cam = scene.getPrimaryCamera();
    const ViewFrameData viewFrame = state.viewSystem.beginFrame(
        *cam,
        state.viewState,
        state.settings.temporalAAMode != 0,
        state.internalRenderWidth,
        state.internalRenderHeight,
        displayWidth,
        displayHeight);
    if (viewFrame.cameraCut) {
        state.frameOrchestrator.resetTemporalHistory();
    }
    const auto& activePipeline = state.compiledPipeline;
    std::vector<PassResources*> scheduledPasses;
    if (activePipeline) {
        state.frameOrchestrator.compileFrame(
            scene, *activePipeline, state.viewState.temporalFrameIndex);
        auto& framePacket = state.frameOrchestrator.framePacket();
        const auto& executionPlan =
            state.frameOrchestrator.executionPlan();
        auto schedule = state.frameExecutionScheduler.schedule(
            framePacket, executionPlan);
        scheduledPasses = std::move(schedule.orderedPasses);

        if (schedule.diagnostics != state.lastFramePlanDiagnostics) {
            if (schedule.diagnostics.empty()) {
                LOG_INFO(
                    "SceneRenderer: RHI frame plan accepted {} passes and {} resources",
                    executionPlan.passes.size(),
                    executionPlan.resources.size());
            } else {
                LOG_WARN(
                    "SceneRenderer: RHI frame plan diagnostics:\n{}",
                    schedule.diagnostics);
            }
            state.lastFramePlanDiagnostics = std::move(schedule.diagnostics);
        }
    } else {
        auto& framePacket = state.frameOrchestrator.framePacket();
        scheduledPasses.reserve(framePacket.passes.size());
        for (auto& pass : framePacket.passes) {
            scheduledPasses.push_back(&pass);
        }
    }

    std::vector<FrameBufferUpload> pendingBufferUploads;
    FrameRuntimeParameterCompiler::populate(
        state,
        scene,
        *cam,
        viewFrame,
        scheduledPasses,
        displayWidth,
        displayHeight,
        currentModelMatrices);
    const auto& sources = state.frameOrchestrator.sourceRegistry();
    state.gpuScene.buildUploads(
        frameIdx,
        scene,
        *cam,
        viewFrame,
        state.viewState,
        sources,
        state.settings,
        state.environmentLightingEnabled,
        state.internalRenderWidth,
        state.internalRenderHeight,
        displayWidth,
        displayHeight,
        pendingBufferUploads);
    auto executionBindings = FrameBindingResolver::resolve(
        device,
        state.sceneResources,
        state.gpuScene,
        sources,
        scheduledPasses,
        state.settings,
        state.viewState,
        frameIdx);

    const uint64_t overlayFrameToken = state.ui
        ? state.ui->acquireFrame()
        : 0;
    if (!renderThread_.running()) {
        if (state.ui && overlayFrameToken != 0) {
            state.ui->releaseFrame(overlayFrameToken);
        }
        return;
    }

    RenderFrameSubmission submission{};
    submission.frameNumber = state.viewState.temporalFrameIndex;
    submission.expectedFrameIndex = frameIdx;
    submission.packet = state.frameOrchestrator.framePacket();
    submission.executionPlan = state.frameOrchestrator.executionPlan();
    submission.bindings = std::move(executionBindings);
    submission.bufferUploads = std::move(pendingBufferUploads);
    submission.overlayFrameToken = overlayFrameToken;
    const uint64_t submittedFrameNumber = submission.frameNumber;
    auto result = std::make_shared<PendingRHIFrame::Result>();
    std::future<void> completion;
    try {
        completion = rhiThread_.submit([this,
         submission = std::move(submission),
         result]() mutable {
            auto& rhiState = *renderState_;
            auto& rhiDevice = *rhiState.rhi.device;
            auto& scheduler = rhiDevice.getFrameScheduler();
            auto& commandList = *rhiState.rhi.commandList;
            bool frameOpen = false;
            bool overlayFrameReleased = false;
            const auto releaseOverlay = [&] {
                if (!overlayFrameReleased && rhiState.ui &&
                    submission.overlayFrameToken != 0) {
                    rhiState.ui->releaseFrame(submission.overlayFrameToken);
                    overlayFrameReleased = true;
                }
            };
            try {
                if (!renderThread_.running()) {
                    LOG_INFO(
                        "Shutdown: cancelling queued RHI frame {}, slot {} before acquire",
                        submission.frameNumber, submission.expectedFrameIndex);
                    result->stage.store(
                        RHIFrameStage::Complete,
                        std::memory_order_release);
                    releaseOverlay();
                    return;
                }
                if (!rhiFrameSequenceValid_) {
                    result->swapchainRebuildRequired = true;
                    releaseOverlay();
                    return;
                }
                result->stage.store(
                    RHIFrameStage::FenceAndAcquire,
                    std::memory_order_release);
                if (!scheduler.beginFrame(commandList)) {
                    rhiFrameSequenceValid_ = false;
                    result->swapchainRebuildRequired = true;
                    releaseOverlay();
                    return;
                }
                frameOpen = true;
                result->stage.store(
                    RHIFrameStage::Recording,
                    std::memory_order_release);
                const uint32_t frameIndex = scheduler.getCurrentFrameIndex();
                if (frameIndex != submission.expectedFrameIndex) {
                    rhiFrameSequenceValid_ = false;
                    result->swapchainRebuildRequired = true;
                    releaseOverlay();
                    scheduler.abortFrame();
                    frameOpen = false;
                    return;
                }

                // beginFrame waited this slot's fence. Only now may mapped
                // per-frame buffers be overwritten.
                for (const auto& upload : submission.bufferUploads) {
                    if (upload.buffer && !upload.bytes.empty()) {
                        upload.buffer->setData(
                            upload.bytes.data(), upload.bytes.size());
                    }
                }

                const auto completedDurations =
                    scheduler.consumeGpuTimestampDurations();
                if (frameIndex < rhiState.gpuTimingNamesPerFrame.size()) {
                    const auto& completedNames =
                        rhiState.gpuTimingNamesPerFrame[frameIndex];
                    const size_t count = std::min(
                        completedNames.size(), completedDurations.size());
                    result->completedTimings.reserve(count);
                    for (size_t index = 0; index < count; ++index) {
                        result->completedTimings.emplace_back(
                            completedNames[index], completedDurations[index]);
                    }
                    rhiState.gpuTimingNamesPerFrame[frameIndex].clear();
                }

                const uint64_t timestampQueryPool =
                    scheduler.getCurrentTimestampQueryPool();
                const uint32_t timestampCapacity = std::min(
                    static_cast<uint32_t>(
                        submission.executionPlan.passes.size() * 2u),
                    256u);
                commandList.resetTimestampQueryPool(
                    timestampQueryPool, timestampCapacity);

                rhiState.rhi.frameExecutor.bindFramePacket(
                    submission.packet);
                const auto swapchainTarget =
                    scheduler.getCurrentSwapchainTarget();

                FrameExecuteContext context;
                context.device = &rhiDevice;
                context.commandList = &commandList;
                context.swapchainTarget = &swapchainTarget;
                context.bindings = &submission.bindings;
                context.externalFeatureExecutor =
                    rhiState.rhi.externalFeatureExecutor.get();
                context.frameIndex = frameIndex;
                context.timestampQueryPool = timestampQueryPool;
                context.timestampQueryCapacity = timestampCapacity;

                const auto executeResult =
                    rhiState.rhi.frameExecutor.executeFrame(
                        submission.executionPlan, context);
                if (frameIndex < rhiState.gpuTimingNamesPerFrame.size()) {
                    rhiState.gpuTimingNamesPerFrame[frameIndex] =
                        executeResult.timestampPassNames;
                }
                scheduler.setCurrentTimestampQueryCount(
                    executeResult.timestampQueryCount);
                if (executeResult.swapchainUsed) {
                    scheduler.markCurrentSwapchainImagePresented();
                }
                if (submission.overlayFrameToken != 0 && rhiState.ui &&
                    executeResult.swapchainUsed) {
                    scheduler.beginOverlay(commandList);
                    commandList.renderOverlay(
                        *rhiState.ui, swapchainTarget,
                        submission.overlayFrameToken);
                    releaseOverlay();
                } else {
                    releaseOverlay();
                }
                result->stage.store(
                    RHIFrameStage::SubmitAndPresent,
                    std::memory_order_release);
                if (!renderThread_.running()) {
                    LOG_INFO(
                        "Shutdown: aborting recorded RHI frame {}, slot {} before submit/present",
                        submission.frameNumber, frameIndex);
                    releaseOverlay();
                    scheduler.abortFrame();
                    frameOpen = false;
                    rhiFrameSequenceValid_ = false;
                    result->stage.store(
                        RHIFrameStage::Complete,
                        std::memory_order_release);
                    return;
                }
                scheduler.submitFrame();
                result->stage.store(
                    RHIFrameStage::Complete,
                    std::memory_order_release);
                frameOpen = false;
                result->submitted = true;
                result->swapchainRebuildRequired =
                    scheduler.isSwapchainRebuildRequired();
            } catch (...) {
                rhiFrameSequenceValid_ = false;
                releaseOverlay();
                if (frameOpen) scheduler.abortFrame();
                throw;
            }
        });
    } catch (...) {
        if (state.ui && overlayFrameToken != 0) {
            state.ui->releaseFrame(overlayFrameToken);
        }
        throw;
    }

    auto pending = std::make_unique<PendingRHIFrame>();
    pending->completion = std::move(completion);
    pending->result = std::move(result);
    pending->frameNumber = submittedFrameNumber;
    pending->frameIndex = frameIdx;
    pendingRHIFrames_.push_back(std::move(pending));
    nextRHIFrameIndex_ = (frameIdx + 1u) %
        std::max(maxFramesInFlight_, 1u);
    state.loggedSubmeshMaterialBindings = true;

    // Publication is ordered but no longer waits per frame. The bounded window
    // resolves the oldest result only before its frame slot is reused.
    state.viewSystem.commitFrame(
        state.viewState,
        viewFrame,
        std::move(currentModelMatrices),
        state.settings.temporalAAMode != 0);
}

} // namespace Tasrovy::Renderer
