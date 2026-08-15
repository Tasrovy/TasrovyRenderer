#include "SceneRendererExecution.h"

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
#include "SceneAnimationSystem.h"
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
#include "../render/Skybox.h"
#include "../render/Texture.hpp"
#include "../ui/UI.h"
#include "../window/Window.h"
#include "Logger.hpp"
#include <imgui.h>
#include <algorithm>
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

std::string formatBytes(uint64_t bytes) {
    constexpr double KiB = 1024.0;
    constexpr double MiB = KiB * 1024.0;
    constexpr double GiB = MiB * 1024.0;

    char buffer[64]{};
    if (bytes >= static_cast<uint64_t>(GiB)) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", static_cast<double>(bytes) / GiB);
    } else if (bytes >= static_cast<uint64_t>(MiB)) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MiB", static_cast<double>(bytes) / MiB);
    } else if (bytes >= static_cast<uint64_t>(KiB)) {
        std::snprintf(buffer, sizeof(buffer), "%.2f KiB", static_cast<double>(bytes) / KiB);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return buffer;
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

std::shared_ptr<Object> replaceTaffyWithStressGrid(
    Scene& scene,
    size_t instanceCount) {
    const auto& sceneObjects = scene.getObjects();
    const auto sourceIt = std::find_if(
        sceneObjects.begin(),
        sceneObjects.end(),
        [](const std::shared_ptr<Object>& object) {
            return object && object->getName() == "Taffy";
        });
    if (sourceIt == sceneObjects.end() || !*sourceIt) {
        return nullptr;
    }

    const auto source = *sourceIt;
    const auto mesh = source->getMesh();
    if (!mesh || mesh->getVertices().empty()) {
        LOG_WARN("Motion: cannot create Taffy stress grid without mesh data");
        return nullptr;
    }

    TSVec3f boundsMin(std::numeric_limits<float>::max());
    TSVec3f boundsMax(std::numeric_limits<float>::lowest());
    for (const auto& vertex : mesh->getVertices()) {
        boundsMin.x = std::min(boundsMin.x, vertex.position.x);
        boundsMin.y = std::min(boundsMin.y, vertex.position.y);
        boundsMin.z = std::min(boundsMin.z, vertex.position.z);
        boundsMax.x = std::max(boundsMax.x, vertex.position.x);
        boundsMax.y = std::max(boundsMax.y, vertex.position.y);
        boundsMax.z = std::max(boundsMax.z, vertex.position.z);
    }

    constexpr uint32_t GridSide = 10;
    constexpr float InstanceScale = 0.075f;
    constexpr float GridSpacing = 0.48f;
    static_assert(GridSide * GridSide * GridSide == 1000);

    const TSVec3f instanceScale = source->getScale() * InstanceScale;
    const TSVec3f localCenter = (boundsMin + boundsMax) * 0.5f;
    const float firstCell =
        -0.5f * static_cast<float>(GridSide - 1) * GridSpacing;

    scene.removeObject(source.get());
    for (size_t index = 0; index < instanceCount; ++index) {
        const uint32_t x = static_cast<uint32_t>(index % GridSide);
        const uint32_t z =
            static_cast<uint32_t>((index / GridSide) % GridSide);
        const uint32_t y =
            static_cast<uint32_t>(index / (GridSide * GridSide));

        auto instance = source->clone();
        instance->setName("Taffy_" + std::to_string(index));
        instance->setScale(instanceScale);
        instance->setPosition(TSVec3f(
            firstCell + static_cast<float>(x) * GridSpacing -
                localCenter.x * instanceScale.x,
            static_cast<float>(y) * GridSpacing -
                boundsMin.y * instanceScale.y,
            firstCell + static_cast<float>(z) * GridSpacing -
                localCenter.z * instanceScale.z));
        scene.addObject(std::move(instance));
    }

    LOG_INFO(
        "Motion: replaced Taffy with {} stress-test instances",
        instanceCount);
    return source;
}

bool drawVec3Control(const char* label, TSVec3f& value, float speed = 0.05f) {
    float data[3] = { value.x, value.y, value.z };
    if (!ImGui::DragFloat3(label, data, speed)) {
        return false;
    }
    value = TSVec3f(data[0], data[1], data[2]);
    return true;
}

TSVec3f radiansToDegrees(TSVec3f radiansValue) {
    constexpr float radiansToDegreesScale = 57.29577951308232f;
    return radiansValue * radiansToDegreesScale;
}

TSVec3f degreesToRadians(TSVec3f degreesValue) {
    constexpr float degreesToRadiansScale = 0.017453292519943295f;
    return degreesValue * degreesToRadiansScale;
}

bool drawEulerDegreesControl(const char* label, TSVec3f& radiansValue, float speed = 0.5f) {
    TSVec3f degreesValue = radiansToDegrees(radiansValue);
    float data[3] = { degreesValue.x, degreesValue.y, degreesValue.z };
    if (!ImGui::DragFloat3(label, data, speed, -360.0f, 360.0f, "%.1f")) {
        return false;
    }
    radiansValue = degreesToRadians(TSVec3f(data[0], data[1], data[2]));
    return true;
}

bool drawColorControl(const char* label, TSVec3f& value) {
    float data[3] = { value.x, value.y, value.z };
    if (!ImGui::ColorEdit3(label, data)) {
        return false;
    }
    value = TSVec3f(data[0], data[1], data[2]);
    return true;
}

const char* materialSurfaceName(MaterialSurface surface) {
    switch (surface) {
    case MaterialSurface::Opaque:
        return "Opaque";
    case MaterialSurface::Masked:
        return "Masked";
    case MaterialSurface::Transparent:
        return "Transparent";
    }
    return "Unknown";
}

bool drawMaterialDebug(Material& material) {
    bool needsPipelineRefresh = false;
    const auto descriptor = material.getDescriptor();
    if (descriptor) {
        ImGui::Text("Material: %s", descriptor->getName().c_str());
        ImGui::TextDisabled(
            "Descriptor: %s",
            descriptor->getSourcePath().generic_string().c_str());
        ImGui::Separator();
        for (const auto& property : descriptor->getProperties()) {
            ImGui::PushID(property.name.c_str());
            switch (property.type) {
            case MaterialPropertyType::Float: {
                float value = material.getFloat(property.name);
                if (ImGui::DragFloat(property.name.c_str(), &value, 0.01f)) {
                    material.setFloat(property.name, value);
                }
                break;
            }
            case MaterialPropertyType::Float3: {
                auto value = material.getVec3(property.name);
                float components[3] = {value.x, value.y, value.z};
                const bool isColor =
                    property.name.find("Color") != std::string::npos ||
                    property.name.find("color") != std::string::npos;
                const bool changed = isColor
                    ? ImGui::ColorEdit3(property.name.c_str(), components)
                    : ImGui::DragFloat3(property.name.c_str(), components, 0.01f);
                if (changed) {
                    material.setVec3(
                        property.name,
                        TSVec3f(components[0], components[1], components[2]));
                }
                break;
            }
            case MaterialPropertyType::Float4: {
                auto value = material.getVec4(property.name);
                float components[4] = {value.x, value.y, value.z, value.w};
                const bool isColor =
                    property.name.find("Color") != std::string::npos ||
                    property.name.find("color") != std::string::npos;
                const bool changed = isColor
                    ? ImGui::ColorEdit4(property.name.c_str(), components)
                    : ImGui::DragFloat4(property.name.c_str(), components, 0.01f);
                if (changed) {
                    material.setVec4(
                        property.name,
                        TSVec4f(
                            components[0], components[1],
                            components[2], components[3]));
                }
                break;
            }
            case MaterialPropertyType::Texture2D:
                break;
            }
            ImGui::PopID();
        }
    }

    int surface = static_cast<int>(material.getSurface());
    const char* surfaceNames[] = { "Opaque", "Masked", "Transparent" };
    if (ImGui::Combo("Surface", &surface, surfaceNames, 3)) {
        material.setSurface(static_cast<MaterialSurface>(surface));
        needsPipelineRefresh = true;
    }

    bool castsShadows = material.castsShadows();
    if (ImGui::Checkbox("Cast Shadows", &castsShadows)) {
        material.setCastShadows(castsShadows);
        needsPipelineRefresh = true;
    }

    float alphaCutoff = material.getAlphaCutoff();
    if (ImGui::DragFloat("Alpha Cutoff", &alphaCutoff, 0.01f, 0.0f, 1.0f)) {
        material.setAlphaCutoff(alphaCutoff);
    }

    if (ImGui::TreeNode("Textures")) {
        static constexpr const char* textureUvModes[] = {
            "Identity",
            "Flip Y",
            "Flip X",
            "Flip X/Y",
            "Swap X/Y",
            "Swap + Flip Y",
            "Swap + Flip X"
        };
        static std::unordered_map<
            const Material*,
            std::unordered_map<std::string, std::array<char, 512>>>
            texturePathDrafts;
        const auto drawTextureSlot =
            [&](const std::string& slot,
                const Material::TextureBinding& binding,
                const MaterialTextureRequirement* requirement) {
                ImGui::PushID(slot.c_str());
                if (requirement) {
                    ImGui::Text(
                        "%s (binding %u)",
                        slot.c_str(), requirement->binding);
                } else {
                    ImGui::Text("%s", slot.c_str());
                    ImGui::TextDisabled("Not bound by the active PBR shader");
                }
                auto& path = texturePathDrafts[&material][slot];
                if (path[0] == '\0' && !binding.path.empty()) {
                    std::snprintf(
                        path.data(), path.size(), "%s", binding.path.c_str());
                }
                ImGui::SetNextItemWidth(-130.0f);
                ImGui::InputText("##TexturePath", path.data(), path.size());
                ImGui::SameLine();
                if (ImGui::Button("Bind")) {
                    const std::filesystem::path texturePath(path.data());
                    if (texturePath.empty() || std::filesystem::is_regular_file(texturePath)) {
                        material.setTexture(slot, path.data());
                        needsPipelineRefresh = true;
                    } else {
                        LOG_WARN(
                            "Material texture does not exist: '{}'",
                            texturePath.string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Default")) {
                    material.clearTexture(slot);
                    path.fill('\0');
                    needsPipelineRefresh = true;
                }
                if (binding.path.empty()) {
                    ImGui::TextDisabled(
                        "Using: %s",
                        requirement ? requirement->defaultTexture.c_str() : "<none>");
                }
                if (path[0] != '\0' &&
                    !std::filesystem::is_regular_file(
                        std::filesystem::path(path.data()))) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                        "File not found");
                }
                auto uvSampling = binding.uvSampling;
                int uvMode = static_cast<int>(uvSampling.mode);
                bool samplingChanged = ImGui::Combo(
                    "UV Orientation",
                    &uvMode,
                    textureUvModes,
                    IM_ARRAYSIZE(textureUvModes));
                samplingChanged |= ImGui::DragFloat2(
                    "UV Scale", &uvSampling.scale.x,
                    0.01f, -10.0f, 10.0f, "%.3f");
                samplingChanged |= ImGui::DragFloat2(
                    "UV Offset", &uvSampling.offset.x,
                    0.01f, -10.0f, 10.0f, "%.3f");
                if (samplingChanged) {
                    uvSampling.mode =
                        static_cast<MaterialTextureUvMode>(uvMode);
                    material.setTextureUvSampling(slot, uvSampling);
                }
                ImGui::Separator();
                ImGui::PopID();
            };

        if (descriptor) {
            for (const auto& property : descriptor->getProperties()) {
                if (property.type != MaterialPropertyType::Texture2D) {
                    continue;
                }
                if (const auto* binding =
                    material.getTextureBinding(property.name)) {
                    drawTextureSlot(
                        property.name,
                        *binding,
                        findPBRMaterialTextureBinding(property.name));
                }
            }
        } else {
            for (const auto& [slot, binding] : material.getTextureBindings()) {
                drawTextureSlot(
                    slot, binding, findPBRMaterialTextureBinding(slot));
            }
        }
        ImGui::TreePop();
    }

    ImGui::Text("Surface: %s", materialSurfaceName(material.getSurface()));
    return needsPipelineRefresh;
}

bool drawObjectDebug(const std::shared_ptr<Object>& object) {
    if (!object) {
        return false;
    }

    bool needsPipelineRefresh = false;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::TreeNodeEx(object.get(), flags, "%s", object->getName().c_str());
    if (!open) {
        return false;
    }

    bool active = object->isActive();
    if (ImGui::Checkbox("Active", &active)) {
        object->setActive(active);
        needsPipelineRefresh = true;
    }

    bool flipProjectionY = object->getFlipProjectionY();
    if (ImGui::Checkbox("Flip Projection Y", &flipProjectionY)) {
        object->setFlipProjectionY(flipProjectionY);
    }
    ImGui::TextDisabled(
        "Front face: %s",
        flipProjectionY ? "Clockwise" : "Counter-Clockwise");

    TSVec3f position = object->getPosition();
    TSVec3f rotation = object->getRotationEuler();
    TSVec3f scale = object->getScale();
    if (drawVec3Control("Position", position)) {
        object->setPosition(position);
    }
    if (drawEulerDegreesControl("Rotation", rotation)) {
        object->setRotation(rotation);
    }
    if (drawVec3Control("Scale", scale, 0.01f)) {
        object->setScale(scale);
    }

    const auto mesh = object->getMesh();
    if (mesh) {
        ImGui::Text("Mesh: %zu vertices, %zu indices", mesh->getVertexCount(), mesh->getIndexCount());
        if (ImGui::TreeNode("Submeshes")) {
            for (size_t index = 0; index < mesh->getSubmeshes().size(); ++index) {
                const auto& submesh = mesh->getSubmeshes()[index];
                ImGui::Text(
                    "%s  offset %u  count %u",
                    submesh.getMaterialName().c_str(),
                    submesh.getIndexOffset(),
                    submesh.getIndexCount());
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::TextUnformatted("Mesh: none");
    }

    if (ImGui::TreeNode("PBR Parameters")) {
        if (mesh && !mesh->getSubmeshes().empty()) {
            for (size_t index = 0; index < mesh->getSubmeshes().size(); ++index) {
                const auto& submesh = mesh->getSubmeshes()[index];
                ImGui::PushID(static_cast<int>(index));
                const std::string label = submesh.getMaterialName().empty()
                    ? "Submesh " + std::to_string(index)
                    : submesh.getMaterialName();
                if (ImGui::TreeNode(label.c_str())) {
                    if (const auto material = object->getSubmeshMaterial(index)) {
                        needsPipelineRefresh |= drawMaterialDebug(*material);
                    } else {
                        ImGui::TextUnformatted("Material: none");
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        } else if (const auto material = object->getMaterial()) {
            needsPipelineRefresh |= drawMaterialDebug(*material);
        } else {
            ImGui::TextUnformatted("Material: none");
        }
        ImGui::TreePop();
    }

    for (const auto& child : object->getChildren()) {
        needsPipelineRefresh |= drawObjectDebug(child);
    }

    ImGui::TreePop();
    return needsPipelineRefresh;
}

} // namespace

struct SceneRendererExecution::PendingRHIFrame {
    struct Result {
        bool submitted = false;
        std::vector<std::pair<std::string, double>> completedTimings;
    };

    std::future<void> completion;
    std::shared_ptr<Result> result;
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
      renderScene_(renderScene),
      renderThread_(renderThread),
      rhiThread_(rhiThread) {
    if (renderState_->ui) {
        renderState_->ui->setDrawCallback([this]() {
            drawSceneDebugUI();
            if (renderState_->resourceMonitor) {
                renderState_->resourceMonitor->draw(
                    renderState_->rhi.device ? renderState_->rhi.device->getDeferredDeletionCount() : 0,
                    renderState_->gpuPassTimings);
            }
        });
    }
    LOG_INFO("SceneRenderer: RHI initialized");
}

SceneRendererExecution::~SceneRendererExecution() {
    try {
        waitForPendingRHIFrame();
    } catch (const std::exception& error) {
        LOG_ERROR("SceneRenderer: RHI worker shutdown failed: {}", error.what());
    }
}

bool SceneRendererExecution::waitForPendingRHIFrame() {
    if (!pendingRHIFrame_) return true;
    auto pending = std::move(pendingRHIFrame_);
    pending->completion.get();
    if (!pending->result) return true;

    renderState_->gpuPassTimings =
        std::move(pending->result->completedTimings);
    if (!pending->result->submitted) {
        renderState_->viewState.invalidate(
            "RHI frame was not submitted", true);
        renderState_->frameOrchestrator.resetTemporalHistory();
    }
    return pending->result->submitted;
}

void SceneRendererExecution::drawSceneDebugUI() {
    auto& state = *renderState_;
    auto lockedScene = renderScene_.lock();
    const auto scene = lockedScene.scene();

    ImGui::SetNextWindowSize(ImVec2(330.0f, 155.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Motion");
    ImGui::Checkbox(
        "Rotate Taffy / Instances",
        &state.settings.taffyRotationEnabled);
    ImGui::TextDisabled(
        state.settings.taffyRotationEnabled
            ? "Rotation: 45 degrees/second around Y"
            : "Rotation paused");
    if (scene && scene->findObject("Taffy")) {
        if (ImGui::Button("Replace with 1000 Small Taffys")) {
            if (auto source = replaceTaffyWithStressGrid(*scene, 1000)) {
                state.taffyStressSource = std::move(source);
                state.animatedTaffy = nullptr;
                state.taffyYawOffset = 0.0f;
                state.lastTaffyAnimationTime = {};
                lockedScene.markDirty();
            }
        }
    } else if (scene && scene->findObject("Taffy_0")) {
        ImGui::TextDisabled("1000-instance stress grid is active");
        if (state.taffyStressSource &&
            ImGui::Button("Restore Original Taffy")) {
            const size_t removed = scene->removeObjectsIf(
                [](const Object& object) {
                    return object.getName().starts_with("Taffy_");
                });
            TSVec3f restoredRotation = state.taffyBaseRotation;
            restoredRotation.y += state.taffyYawOffset;
            state.taffyStressSource->setRotation(restoredRotation);
            state.taffyStressSource->setName("Taffy");
            scene->addObject(std::move(state.taffyStressSource));
            state.animatedTaffy = nullptr;
            state.taffyYawOffset = 0.0f;
            state.lastTaffyAnimationTime = {};
            lockedScene.markDirty();
            LOG_INFO(
                "Motion: restored original Taffy after removing {} instances",
                removed);
        }
    } else {
        ImGui::TextDisabled("Taffy source object not found");
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Objects");
    if (!scene) {
        ImGui::TextUnformatted("No scene");
    } else {
        ImGui::Text("Scene: %s", scene->getName().c_str());
        ImGui::Text("Objects: %zu", scene->getObjectCount());
        ImGui::Separator();
        bool needsPipelineRefresh = false;
        const bool stressGridActive = scene->findObject("Taffy_0") != nullptr;
        if (stressGridActive) {
            ImGui::TextDisabled(
                "Taffy stress instances are hidden from this list");
        }
        for (const auto& object : scene->getObjects()) {
            if (stressGridActive && object &&
                object->getName().starts_with("Taffy_")) {
                continue;
            }
            needsPipelineRefresh |= drawObjectDebug(object);
        }
        if (needsPipelineRefresh) {
            lockedScene.markDirty();
        }
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Inspector");
    const float fps = ImGui::GetIO().Framerate;
    const float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

    const uint64_t meshBufferBytes =
        state.sceneResources.meshBufferBytes();
    const uint64_t skyboxBufferBytes =
        state.sceneResources.skyboxBufferBytes();

    uint64_t uniformResidentBytes = 0;
    uint64_t uniformPerFrameBytes = 0;
    for (auto& pass : state.rhi.frameExecutor.compiledPipeline().passes()) {
        if (!pass.uniformBuffers.empty() && pass.uniformBuffers.front()) {
            uniformPerFrameBytes += pass.uniformBuffers.front()->getSize();
        }
        for (const auto& buffer : pass.uniformBuffers) {
            if (buffer) {
                uniformResidentBytes += buffer->getSize();
            }
        }
    }
    const uint64_t uniformBytesPerSecond =
        static_cast<uint64_t>(static_cast<double>(uniformPerFrameBytes) * static_cast<double>(fps));

    ImGui::Text("Tasrovy RHI frame");
    ImGui::Text("FPS: %.1f  Frame: %.2f ms", fps, frameMs);
    const char* pipelineNames[] = {"PBR", "Deferred"};
    if (lockedScene.pipeline()) {
        if (lockedScene.pipeline()->getName() == "Deferred") {
            state.settings.selectedPipelineIndex = 1;
        } else if (lockedScene.pipeline()->getName() == "PBR") {
            state.settings.selectedPipelineIndex = 0;
        }
    }
    if (ImGui::Combo("Pipeline", &state.settings.selectedPipelineIndex, pipelineNames, 2)) {
        lockedScene.pipeline() = state.settings.selectedPipelineIndex == 1
            ? std::static_pointer_cast<PipelineBase>(DeferredPipeline::create())
            : std::static_pointer_cast<PipelineBase>(PBRPipeline::create());
        state.settings.debugOutputResource.clear();
        state.settings.debugOutputSemantic = DebugTextureSemantic::FinalOutput;
        lockedScene.markDirty();
        LOG_INFO(
            "SceneRenderer: switched pipeline to '{}'",
            lockedScene.pipeline()->getName());
    }
    ImGui::Text("Passes: %zu", state.rhi.frameExecutor.compiledPipeline().size());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Render Graph: %s",
        state.frameOrchestrator.renderGraph().isValid() ? "valid" : "invalid");
    if (ImGui::CollapsingHeader("Render Graph")) {
        const auto& framePasses =
            state.frameOrchestrator.framePacket().passes;
        for (size_t index = 0; index < framePasses.size(); ++index) {
            ImGui::Text(
                "%zu. %s",
                index,
                framePasses[index].name.c_str());
        }
        if (ImGui::TreeNode("Dependencies")) {
            for (const auto& edge :
                 state.frameOrchestrator.renderGraph().getEdges()) {
                if (edge.producer >= framePasses.size() ||
                    edge.consumer >= framePasses.size()) {
                    continue;
                }
                ImGui::BulletText(
                    "%s -> %s  [%s: %s]",
                    framePasses[edge.producer].name.c_str(),
                    framePasses[edge.consumer].name.c_str(),
                    renderGraphHazardName(edge.hazard),
                    edge.resource.c_str());
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Resource Lifetimes")) {
            for (const auto& lifetime :
                 state.frameOrchestrator.renderGraph().getResourceLifetimes()) {
                ImGui::BulletText(
                    "%s  [%zu, %zu]%s",
                    lifetime.resource.c_str(),
                    lifetime.firstUse,
                    lifetime.lastUse,
                    lifetime.external ? " external" : "");
            }
            ImGui::TreePop();
        }
        for (const auto& diagnostic :
             state.frameOrchestrator.renderGraph().getDiagnostics()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                "%s",
                diagnostic.c_str());
        }
    }
    ImGui::Text("Meshes: %zu", state.sceneResources.meshCount());
    ImGui::Text(
        "Render Textures: %zu",
        state.rhi.frameExecutor.textures().size());
    ImGui::Text(
        "Material Textures: %zu",
        state.sceneResources.materialTextureCount());
    if (ImGui::CollapsingHeader("Data Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Uniform/frame: %s", formatBytes(uniformPerFrameBytes).c_str());
        ImGui::Text("Uniform/sec: %s/s", formatBytes(uniformBytesPerSecond).c_str());
        ImGui::Text("Uniform resident: %s", formatBytes(uniformResidentBytes).c_str());
        ImGui::Text("Mesh buffers: %s", formatBytes(meshBufferBytes).c_str());
        ImGui::Text("Skybox buffers: %s", formatBytes(skyboxBufferBytes).c_str());
        ImGui::Text(
            "Render textures: %s",
            formatBytes(state.rhi.frameExecutor.allocatedBytes()).c_str());
        ImGui::Text(
            "Skybox variants: %zu",
            state.sceneResources.skyboxVariants().size());
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* shadowTechniques[] = {
            "Shadow Map",
            "Cascaded Shadow Maps",
            "Virtual Shadow Maps (resident pages)"
        };
        ImGui::Combo(
            "Technique",
            &state.settings.shadowTechnique,
            shadowTechniques,
            IM_ARRAYSIZE(shadowTechniques));
        ImGui::SliderFloat("Slope Bias", &state.settings.shadowSlopeBias, 0.0f, 0.02f, "%.5f");
        ImGui::SliderFloat("Minimum Bias", &state.settings.shadowMinimumBias, 0.0f, 0.01f, "%.5f");
        ImGui::SliderFloat("Strength", &state.settings.shadowStrength, 0.0f, 1.0f);
        ImGui::SliderFloat(
            "CSM Distance", &state.settings.csmMaximumDistance, 5.0f, 150.0f, "%.1f");
        if (state.settings.shadowTechnique != static_cast<int>(ShadowTechnique::ShadowMap)) {
            ImGui::SliderFloat(
                "CSM Split Lambda", &state.settings.csmSplitLambda, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat(
                "CSM Blend", &state.settings.csmBlendFraction, 0.0f, 0.30f, "%.2f");
            if (state.settings.shadowTechnique ==
                static_cast<int>(ShadowTechnique::VirtualShadowMap)) {
                ImGui::TextDisabled(
                    "VSM: 4096 atlas, four fixed resident 2048 pages");
            } else {
                ImGui::TextDisabled(
                    "4 cascades x 2048; texel-snapped stable projections");
            }
        } else {
            ImGui::TextDisabled(
                "Single 2048 shadow map covering the full shadow distance");
        }
    }

    if (ImGui::CollapsingHeader("Advanced Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Adaptive PCSS", &state.settings.pcssEnabled);
        if (state.settings.pcssEnabled) {
            ImGui::SliderFloat("PCSS Light Size", &state.settings.pcssLightSize, 0.001f, 0.08f, "%.4f");
            ImGui::SliderFloat(
                "PCSS Max Radius", &state.settings.pcssMaxFilterRadius, 0.002f, 0.12f, "%.4f");
        }

        ImGui::Checkbox("HBAO", &state.settings.ssaoEnabled);
        if (state.settings.ssaoEnabled) {
            ImGui::SliderFloat("HBAO Screen Radius", &state.settings.ssaoRadiusPixels, 2.0f, 64.0f);
            ImGui::SliderFloat("HBAO World Radius", &state.settings.ssaoWorldRadius, 0.05f, 5.0f);
            ImGui::SliderFloat("HBAO Intensity", &state.settings.ssaoIntensity, 0.0f, 4.0f);
            ImGui::SliderFloat("HBAO Bias", &state.settings.ssaoBias, 0.0f, 0.2f, "%.4f");
        }

        if (ImGui::Checkbox("SSR", &state.settings.ssrEnabled)) {
            // SSR is part of the temporal input, so toggling it changes the
            // history's meaning and requires a fresh accumulation.
            state.viewState.temporalHistoryValid = false;
        }
        if (state.settings.ssrEnabled) {
            ImGui::SliderFloat("SSR Max Distance", &state.settings.ssrMaxDistance, 0.5f, 30.0f);
            ImGui::SliderFloat("SSR Step Size", &state.settings.ssrStepSize, 0.02f, 1.0f);
            ImGui::SliderFloat("SSR Thickness", &state.settings.ssrThickness, 0.01f, 1.0f);
            ImGui::SliderFloat("SSR Intensity", &state.settings.ssrIntensity, 0.0f, 1.0f);
        }
    }

    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* temporalModes[] = {
            "Off (Spatial Upscale)",
            "TAA (Native Resolution)",
            "TAAU (Display Resolution)"
        };
        if (ImGui::Combo(
                "Temporal AA", &state.settings.temporalAAMode,
                temporalModes, static_cast<int>(std::size(temporalModes)))) {
            state.viewState.temporalHistoryValid = false;
            state.viewState.previousModelMatrices.clear();
            state.frameOrchestrator.resetTemporalHistory();
            // Native TAA renders the entire deferred graph at the display
            // extent; Off and TAAU restore the configured fixed internal size.
            state.internalExtentDirty = true;
        }
        if (state.settings.temporalAAMode != 0) {
            ImGui::SliderFloat(
                "History Weight", &state.settings.taaHistoryWeight, 0.0f, 0.98f);
        }
        if (ImGui::SliderFloat(
                "Internal Resolution",
                &state.settings.internalResolutionPercent,
                25.0f,
                200.0f,
                "%.0f%%")) {
            // The setting is dormant in Native TAA and is applied when Off or
            // TAAU is selected again.
            state.internalExtentDirty = state.settings.temporalAAMode != 1;
            state.viewState.temporalHistoryValid = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) {
            state.settings.internalResolutionPercent = 100.0f;
            state.internalExtentDirty = state.settings.temporalAAMode != 1;
            state.viewState.temporalHistoryValid = false;
        }
        ImGui::Text(
            "Internal: %u x %u  Display: %u x %u",
            state.internalRenderWidth,
            state.internalRenderHeight,
            state.rhi.device->getFrameScheduler().getWidth(),
            state.rhi.device->getFrameScheduler().getHeight());
        ImGui::Text("Requested internal scale: %.0f%%", state.settings.internalResolutionPercent);
        ImGui::Text(
            "History: %s  frame: %llu",
            state.viewState.historyStatus.c_str(),
            static_cast<unsigned long long>(state.viewState.temporalFrameIndex));
        ImGui::TextDisabled(
            "Previous jitter: %.6f, %.6f",
            state.viewState.previousJitterUv.x,
            state.viewState.previousJitterUv.y);
        if (state.settings.temporalAAMode == 1) {
            ImGui::TextDisabled(
                "Native TAA forces 100%%; the requested scale is retained for TAAU/Off.");
        }
        ImGui::Separator();
        bool dofChanged = ImGui::Checkbox(
            "Depth of Field (Pre-TAA)", &state.settings.depthOfFieldEnabled);
        if (state.settings.depthOfFieldEnabled) {
            dofChanged |= ImGui::DragFloat(
                "Focus Distance", &state.settings.dofFocusDistance,
                0.05f, 0.05f, 100.0f, "%.2f");
            dofChanged |= ImGui::DragFloat(
                "Focus Range", &state.settings.dofFocusRange,
                0.05f, 0.05f, 25.0f, "%.2f");
            dofChanged |= ImGui::SliderFloat(
                "DOF Max Radius", &state.settings.dofMaxBlurRadius,
                0.5f, 20.0f, "%.1f px");
            dofChanged |= ImGui::SliderFloat(
                "DOF Strength", &state.settings.dofStrength, 0.0f, 2.0f);
        }
        if (dofChanged) {
            state.viewState.temporalHistoryValid = false;
        }
        ImGui::Checkbox("Motion Blur", &state.settings.motionBlurEnabled);
        if (state.settings.motionBlurEnabled) {
            ImGui::SliderFloat(
                "Motion Strength", &state.settings.motionBlurStrength, 0.0f, 2.0f);
            ImGui::SliderFloat(
                "Motion Max Radius", &state.settings.motionBlurMaxRadius,
                1.0f, 64.0f, "%.1f px");
            ImGui::SliderInt(
                "Motion Samples", &state.settings.motionBlurSamples, 4, 16);
        }
        ImGui::Separator();
        ImGui::Checkbox("Bloom", &state.settings.bloomEnabled);
        ImGui::SliderFloat("Bloom Threshold", &state.settings.bloomThreshold, 0.0f, 10.0f);
        ImGui::SliderFloat("Bloom Intensity", &state.settings.bloomIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Bloom Radius", &state.settings.bloomRadius, 0.25f, 4.0f);
        ImGui::SliderFloat("Exposure", &state.settings.exposure, 0.05f, 5.0f);
        ImGui::Separator();
        ImGui::Checkbox("Normal Outline", &state.settings.outlineEnabled);
        if (state.settings.outlineEnabled) {
            ImGui::SliderFloat("Outline Threshold", &state.settings.outlineThreshold, 0.001f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &state.settings.outlineThickness, 0.5f, 5.0f);
            ImGui::SliderFloat("Outline Strength", &state.settings.outlineStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Softness", &state.settings.outlineSoftness, 0.001f, 0.5f);
            float outlineColor[3] = {
                state.settings.outlineColor.x, state.settings.outlineColor.y, state.settings.outlineColor.z
            };
            if (ImGui::ColorEdit3("Outline Color", outlineColor)) {
                state.settings.outlineColor = TSVec3f(
                    outlineColor[0], outlineColor[1], outlineColor[2]);
            }
        }
        ImGui::Checkbox(
            "Temporal Outline Denoise",
            &state.settings.outlineTemporalDenoise);
        if (state.settings.outlineTemporalDenoise) {
            ImGui::SliderFloat(
                "Outline History Weight",
                &state.settings.outlineHistoryWeight,
                0.0f,
                0.98f,
                "%.2f");
        }
    }

    if (ImGui::CollapsingHeader("Debug Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Frame packet: %zu passes, %zu draws",
            state.frameOrchestrator.framePacket().passes.size(),
            [&]() {
                size_t drawCount = 0;
                for (const auto& pass :
                     state.frameOrchestrator.framePacket().passes) {
                    drawCount += pass.draws.size();
                }
                return drawCount;
            }());
        ImGui::TextDisabled(
            "RHI plan: %zu passes, %zu resources, %zu diagnostics",
            state.frameOrchestrator.executionPlan().passes.size(),
            state.frameOrchestrator.executionPlan().resources.size(),
            state.frameOrchestrator.executionPlan().diagnostics.size());

        struct DebugOutputOption {
            std::string label;
            std::string resource;
            DebugTextureSemantic semantic =
                DebugTextureSemantic::Color;
        };
        const auto semanticName = [](DebugTextureSemantic semantic) {
            switch (semantic) {
            case DebugTextureSemantic::FinalOutput: return "Final";
            case DebugTextureSemantic::Color: return "Color";
            case DebugTextureSemantic::OutlineBlackLines: return "Outline";
            case DebugTextureSemantic::Normal: return "Normal";
            case DebugTextureSemantic::Velocity: return "Velocity";
            case DebugTextureSemantic::RawDepth: return "Raw Depth";
            case DebugTextureSemantic::SceneLinearDepth: return "Linear Depth";
            case DebugTextureSemantic::HiZLinearDepth: return "Hi-Z Depth";
            case DebugTextureSemantic::Mask: return "Mask";
            }
            return "Unknown";
        };
        const auto classifyColorResource = [](const std::string& resource) {
            if (resource == "GBufferNormal") {
                return DebugTextureSemantic::Normal;
            }
            if (resource == "GBufferVelocity") {
                return DebugTextureSemantic::Velocity;
            }
            if (resource.starts_with("HiZ")) {
                return DebugTextureSemantic::HiZLinearDepth;
            }
            if (resource == "OutlineHistory") {
                return DebugTextureSemantic::Mask;
            }
            return DebugTextureSemantic::Color;
        };
        std::vector<DebugOutputOption> options;
        options.push_back({
            "Final Output", "",
            DebugTextureSemantic::FinalOutput});
        options.push_back({
            "Outline Only (Black Lines)",
            OutlineOnlyDebugOutput,
            DebugTextureSemantic::OutlineBlackLines});

        for (const auto& pass : state.rhi.frameExecutor.compiledPipeline().passes()) {
            if (!pass.framePass || pass.rhiPasses.empty() || !pass.rhiPasses.front()) {
                continue;
            }

            const auto& debugPass = *pass.rhiPasses.front();
            uint32_t colorIndex = 0;
            for (const auto& attachment : debugPass.getDesc().colorAttachments) {
                if (attachment.image) {
                    const auto semantic =
                        classifyColorResource(attachment.name);
                    options.push_back({
                        pass.framePass->getName() + " / Color" +
                            std::to_string(colorIndex) + " / " + attachment.name +
                            " [" + semanticName(semantic) + "]",
                        attachment.name,
                        semantic
                    });
                }
                ++colorIndex;
            }

            if (const auto& depth = debugPass.getDesc().depthAttachment;
                depth && depth->image) {
                const auto semantic = depth->name == "SceneDepth"
                    ? DebugTextureSemantic::SceneLinearDepth
                    : DebugTextureSemantic::RawDepth;
                options.push_back({
                    pass.framePass->getName() + " / Depth / " + depth->name +
                        " [" + semanticName(semantic) + "]",
                    depth->name,
                    semantic
                });
            }
        }

        int currentOption = 0;
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            if (options[static_cast<size_t>(i)].resource ==
                    state.settings.debugOutputResource &&
                options[static_cast<size_t>(i)].semantic ==
                    state.settings.debugOutputSemantic) {
                currentOption = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Display", options[static_cast<size_t>(currentOption)].label.c_str())) {
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                const bool selected = i == currentOption;
                if (ImGui::Selectable(options[static_cast<size_t>(i)].label.c_str(), selected)) {
                    state.settings.debugOutputResource = options[static_cast<size_t>(i)].resource;
                    state.settings.debugOutputSemantic =
                        options[static_cast<size_t>(i)].semantic;
                    LOG_INFO(
                        "SceneRenderer: debug output '{}'",
                        state.settings.debugOutputResource.empty()
                            ? std::string("Final Output")
                            : state.settings.debugOutputResource);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (state.settings.debugOutputSemantic ==
            DebugTextureSemantic::Velocity) {
            ImGui::SliderFloat(
                "Velocity Preview Scale",
                &state.settings.debugVelocityScale,
                1.0f,
                256.0f,
                "%.1f");
        }
        if (state.settings.debugOutputSemantic ==
                DebugTextureSemantic::SceneLinearDepth ||
            state.settings.debugOutputSemantic ==
                DebugTextureSemantic::HiZLinearDepth) {
            ImGui::SliderFloat(
                "Depth Preview Range",
                &state.settings.debugDepthRange,
                1.0f,
                500.0f,
                "%.1f");
        }
    }
    ImGui::Separator();

    if (!scene) {
        ImGui::TextUnformatted("No scene");
        ImGui::End();
        return;
    }

    ImGui::Text("Scene: %s", scene->getName().c_str());
    ImGui::Text(
        "Objects: %zu  Cameras: %zu  Lights: %zu",
        scene->getObjectCount(),
        scene->getCameraCount(),
        scene->getLightCount());

    if (ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& skyboxVariants = state.sceneResources.skyboxVariants();
        if (skyboxVariants.empty()) {
            ImGui::TextUnformatted("No skybox variants");
        } else {
            const int selectedSkyboxIndex =
                state.sceneResources.selectedSkyboxIndex();
            const auto& current =
                skyboxVariants[static_cast<size_t>(selectedSkyboxIndex)];
            if (ImGui::BeginCombo("Environment", current.name.c_str())) {
                for (int i = 0; i < static_cast<int>(skyboxVariants.size()); ++i) {
                    const bool selected = i == selectedSkyboxIndex;
                    if (ImGui::Selectable(
                            skyboxVariants[static_cast<size_t>(i)].name.c_str(),
                            selected)) {
                        state.sceneResources.selectSkybox(i);
                        state.loggedSkyboxDrawState = false;
                        LOG_INFO(
                            "SceneRenderer: switched skybox to '{}'",
                            state.sceneResources.activeSkyboxName());
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Text("Path: %s", current.path.c_str());
            ImGui::TextDisabled("IBL: disabled");
        }
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* camera = scene->getPrimaryCamera()) {
            ImGui::Text("Primary: %s", camera->getName().c_str());

            TSVec3f position = camera->getPosition();
            TSVec3f rotation = camera->getRotationEuler();
            float fov = camera->getFOV();
            float aspect = camera->getAspect();

            if (drawVec3Control("Position##Camera", position)) {
                camera->setPosition(position);
            }
            if (drawEulerDegreesControl("Rotation##Camera", rotation)) {
                camera->setRotation(rotation);
            }
            if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f)) {
                camera->setFOV(fov);
            }
            if (ImGui::DragFloat("Aspect", &aspect, 0.01f, 0.1f, 4.0f)) {
                camera->setAspect(aspect);
            }
        } else {
            ImGui::TextUnformatted("No primary camera");
        }
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        size_t index = 0;
        for (const auto& lightPtr : scene->getLights()) {
            auto* light = lightPtr.get();
            if (!light) {
                continue;
            }

            const std::string label =
                light->getName().empty()
                    ? "Light " + std::to_string(index)
                    : light->getName() + "##Light" + std::to_string(index);
            if (ImGui::TreeNode(label.c_str())) {
                TSVec3f direction = light->getDirection();
                TSVec3f color = light->getColor();
                float intensity = light->getIntensity();

                if (drawVec3Control("Direction", direction, 0.01f)) {
                    light->setDirection(direction);
                }
                if (drawColorControl("Color", color)) {
                    light->setColor(color);
                }
                if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 1000.0f)) {
                    light->setIntensity(intensity);
                }

                if (auto* point = dynamic_cast<PointLight*>(light)) {
                    TSVec3f position = point->getPosition();
                    float constant = point->getConstant();
                    float linear = point->getLinear();
                    float quadratic = point->getQuadratic();
                    if (drawVec3Control("Position", position)) {
                        point->setPosition(position);
                    }
                    if (ImGui::DragFloat("Constant", &constant, 0.01f, 0.0f, 10.0f)) {
                        point->setConstant(constant);
                    }
                    if (ImGui::DragFloat("Linear", &linear, 0.01f, 0.0f, 10.0f)) {
                        point->setLinear(linear);
                    }
                    if (ImGui::DragFloat("Quadratic", &quadratic, 0.01f, 0.0f, 10.0f)) {
                        point->setQuadratic(quadratic);
                    }
                }

                if (auto* area = dynamic_cast<AreaLight*>(light)) {
                    TSVec3f position = area->getPosition();
                    float width = area->getWidth();
                    float height = area->getHeight();
                    bool twoSided = area->isTwoSided();
                    if (drawVec3Control("Position", position)) {
                        area->setPosition(position);
                    }
                    if (ImGui::DragFloat("Width", &width, 0.05f, 0.01f, 100.0f)) {
                        area->setWidth(width);
                    }
                    if (ImGui::DragFloat("Height", &height, 0.05f, 0.01f, 100.0f)) {
                        area->setHeight(height);
                    }
                    if (ImGui::Checkbox("Two Sided", &twoSided)) {
                        area->setTwoSided(twoSided);
                    }
                }

                if (auto* spot = dynamic_cast<SpotLight*>(light)) {
                    TSVec3f position = spot->getPosition();
                    float cutoff = spot->getCutoff();
                    if (drawVec3Control("Position", position)) {
                        spot->setPosition(position);
                    }
                    if (ImGui::DragFloat("Cutoff", &cutoff, 0.1f, 0.0f, 90.0f)) {
                        spot->setCutoff(cutoff);
                    }
                }

                ImGui::TreePop();
            }
            ++index;
        }
    }

    if (ImGui::CollapsingHeader("Render Passes")) {
        for (const auto& pass : state.rhi.frameExecutor.compiledPipeline().passes()) {
            if (!pass.framePass) {
                continue;
            }
            ImGui::Text(
                "%s  objects %zu  swapchain %d",
                pass.framePass->getName().c_str(),
                pass.framePass->getObjectIds().size(),
                pass.usesSwapchain ? 1 : 0);
        }
    }

    ImGui::End();
}

void SceneRendererExecution::run() {
    try {
        renderLoop();
        waitForPendingRHIFrame();
    } catch (const std::exception& error) {
        LOG_ERROR("SceneRenderer: render/RHI pipeline stopped: {}", error.what());
    }
}

void SceneRendererExecution::renderLoop() {
    uint64_t appliedResizeGeneration = 0;
    while (renderThread_.running()) {
        // Every shared RHI object below is single-owner while a frame job is
        // active. Resolve that job before inspecting scheduler or pipeline
        // state. Frame construction later overlaps the main/game thread.
        waitForPendingRHIFrame();
        if (!renderThread_.running()) break;
        const auto submitted = renderScene_.snapshot();

        if (!submitted.scene) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (submitted.pipeline) {
            submitted.pipeline->applyConfiguration(
                RendererFeaturePolicy::configuration(
                    renderState_->settings));
        }
        const bool configurationOutdated =
            renderState_->compiledPipeline &&
            renderState_->compiledPipeline->getConfigurationVersion() !=
                renderState_->compiledPipelineConfigurationVersion;
        if (submitted.dirty) {
            activeScene_ = submitted.scene->clone();
            rebuildRenderGraph(activeScene_);
            renderScene_.acknowledge(submitted.version);
        } else if (configurationOutdated) {
            if (!activeScene_) {
                activeScene_ = submitted.scene->clone();
            }
            rebuildRenderGraph(activeScene_);
        }

        const auto scene = activeScene_;
        if (!scene) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
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
        if (windowSizeChanged || frameScheduler.isSwapchainRebuildRequired()) {
            bool recreated = false;
            rhiThread_.invoke([&] {
                recreated = frameScheduler.recreateSwapchain(
                    static_cast<uint32_t>(framebuffer.width),
                    static_cast<uint32_t>(framebuffer.height));
            });
            if (!recreated) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            appliedResizeGeneration = framebuffer.resizeGeneration;
            swapchainRecreated = true;
            if (auto* cam = scene->getPrimaryCamera()) {
                cam->setAspect(
                    static_cast<float>(frameScheduler.getWidth()) /
                    static_cast<float>(frameScheduler.getHeight()));
            }
        }

        const auto desiredInternalExtent = renderState_->settings.temporalAAMode == 1
            ? std::pair<uint32_t, uint32_t>{
                frameScheduler.getWidth(), frameScheduler.getHeight()}
            : calculateInternalExtent(
                frameScheduler.getWidth(),
                frameScheduler.getHeight(),
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
            const auto pipeline = renderScene_.snapshot().pipeline;
            if (pipeline) {
                rebuildDisplayResources(*pipeline);
            } else {
                rebuildRenderGraph(scene);
            }
        }

        renderFrame(*scene);
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

    std::string preferredSkyboxPath;
    for (const auto& object : objects) {
        const auto skybox = std::dynamic_pointer_cast<Skybox>(object);
        if (skybox && skybox->getCubemap()) {
            preferredSkyboxPath = skybox->getCubemap()->getFilePath();
            break;
        }
    }
    state.environmentLightingEnabled = false;
    state.sceneResources.prepareSkyboxVariants(
        device, state.rhi.persistentResourceScope, preferredSkyboxPath);
    if (state.sceneResources.skyCubemap()) {
        state.sceneResources.rebuildSkyboxGeometry(
            device, frameScheduler, *state.rhi.commandList,
            state.rhi.sceneResourceScope, true);
        LOG_INFO(
            "SceneRenderer: active skybox '{}' loaded, indices {}",
            state.sceneResources.activeSkyboxName(),
            state.sceneResources.skyboxIndexCount());
    }
}

void SceneRendererExecution::rebuildRenderGraph(
    const std::shared_ptr<Scene>& scene) {
    if (!scene) return;
    if (!rhiThread_.isCurrentThread()) {
        waitForPendingRHIFrame();
        rhiThread_.invoke([this, scene] { rebuildRenderGraph(scene); });
        return;
    }
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();
    auto pipeline = renderScene_.snapshot().pipeline;

    if (!pipeline) {
        pipeline = DeferredPipeline::create();
        renderScene_.adoptPipelineIfEmpty(pipeline);
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
    state.resetSceneTransientState();
    device.resetResourceScope(state.rhi.displayResourceScope);
    device.resetResourceScope(state.rhi.sceneResourceScope);

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
        return;
    }
    auto& state = *renderState_;
    auto& device = *state.rhi.device;
    auto& frameScheduler = device.getFrameScheduler();

    // Display history and swapchain-facing pass descriptions depend on the
    // window extent. Internal GBuffer resources remain alive when the aspect
    // ratio (and therefore the fixed-height internal extent) is unchanged.
    frameScheduler.waitForInFlightFrames();
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

    if (!scene.getPrimaryCamera() || !state.compiledPipeline) {
        return;
    }

    // No other frame job is active when renderFrame is entered. This index is
    // therefore the exact slot the RHI worker will acquire for this packet.
    const uint32_t frameIdx = frameScheduler.getCurrentFrameIndex();
    const uint32_t displayWidth = frameScheduler.getWidth();
    const uint32_t displayHeight = frameScheduler.getHeight();
    std::unordered_map<const Object*, TSMat4f> currentModelMatrices;
    currentModelMatrices.reserve(state.viewState.previousModelMatrices.size() + 4u);

    SceneAnimationSystem::update(
        scene, state, std::chrono::steady_clock::now());

    auto* cam = scene.getPrimaryCamera();
    const ViewFrameData viewFrame = state.viewSystem.beginFrame(
        *cam,
        state.viewState,
        state.settings.temporalAAMode != 0,
        state.internalRenderWidth,
        state.internalRenderHeight);
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

    const bool drawUI = state.ui && state.ui->beginFrame(
        displayWidth, displayHeight);

    auto packetForRHI = state.frameOrchestrator.framePacket();
    auto planForRHI = state.frameOrchestrator.executionPlan();
    auto result = std::make_shared<PendingRHIFrame::Result>();
    auto completion = rhiThread_.submit(
        [this,
         expectedFrameIndex = frameIdx,
         packet = std::move(packetForRHI),
         plan = std::move(planForRHI),
         bindings = std::move(executionBindings),
         uploads = std::move(pendingBufferUploads),
         drawUI,
         result]() mutable {
            auto& rhiState = *renderState_;
            auto& rhiDevice = *rhiState.rhi.device;
            auto& scheduler = rhiDevice.getFrameScheduler();
            auto& commandList = *rhiState.rhi.commandList;
            bool frameOpen = false;
            try {
                if (!scheduler.beginFrame(commandList)) {
                    commandList.useNativeCommandBuffer(0);
                    return;
                }
                frameOpen = true;
                const uint32_t frameIndex = scheduler.getCurrentFrameIndex();
                if (frameIndex != expectedFrameIndex) {
                    throw std::runtime_error(
                        "RHI frame slot changed after packet publication");
                }

                // beginFrame waited this slot's fence. Only now may mapped
                // per-frame buffers be overwritten.
                for (const auto& upload : uploads) {
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
                    static_cast<uint32_t>(plan.passes.size() * 2u), 256u);
                commandList.resetTimestampQueryPool(
                    timestampQueryPool, timestampCapacity);

                rhiState.rhi.frameExecutor.bindFramePacket(packet);
                const auto swapchainTarget =
                    scheduler.getCurrentSwapchainTarget();

                FrameExecuteContext context;
                context.device = &rhiDevice;
                context.commandList = &commandList;
                context.swapchainTarget = &swapchainTarget;
                context.overlay = rhiState.ui.get();
                context.bindings = &bindings;
                context.frameIndex = frameIndex;
                context.drawOverlay = drawUI;
                context.timestampQueryPool = timestampQueryPool;
                context.timestampQueryCapacity = timestampCapacity;

                const auto executeResult =
                    rhiState.rhi.frameExecutor.executeFrame(plan, context);
                if (frameIndex < rhiState.gpuTimingNamesPerFrame.size()) {
                    rhiState.gpuTimingNamesPerFrame[frameIndex] =
                        executeResult.timestampPassNames;
                }
                scheduler.setCurrentTimestampQueryCount(
                    executeResult.timestampQueryCount);
                if (executeResult.swapchainUsed) {
                    scheduler.markCurrentSwapchainImagePresented();
                }
                scheduler.submitFrame();
                frameOpen = false;
                commandList.useNativeCommandBuffer(0);
                result->submitted = true;
            } catch (...) {
                if (frameOpen) scheduler.abortFrame();
                commandList.useNativeCommandBuffer(0);
                throw;
            }
        });

    pendingRHIFrame_ = std::make_unique<PendingRHIFrame>();
    pendingRHIFrame_->completion = std::move(completion);
    pendingRHIFrame_->result = std::move(result);
    state.loggedSubmeshMaterialBindings = true;

    // Publication is speculative but ordered. The next render iteration waits
    // the RHI result and invalidates history if acquire/record/submit failed.
    state.viewSystem.commitFrame(
        state.viewState,
        viewFrame,
        std::move(currentModelMatrices),
        state.settings.temporalAAMode != 0);
}

} // namespace Tasrovy::Renderer
