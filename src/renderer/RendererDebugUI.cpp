#include "RendererDebugUI.h"
#include "RenderScene.h"
#include "SceneRendererComponents.h"

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
#include "SkyboxGeometry.h"
#include "../render/FrameCompiler.h"
#include "../render/FramePacket.h"
#include "../render/Material.h"
#include "../render/MaterialDescriptor.h"
#include "../render/MaterialTechnique.h"
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
#include "../render/StylizedPBRPipeline.h"
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
inline constexpr const char* TaffyStressPrefix = "TaffyStress_";

size_t countTaffyStressInstances(const Scene& scene) {
    return static_cast<size_t>(std::count_if(
        scene.getObjects().begin(),
        scene.getObjects().end(),
        [](const std::shared_ptr<Object>& object) {
            return object && object->getName().starts_with(TaffyStressPrefix);
        }));
}

bool createTaffyStressInstances(Scene& scene, size_t instanceCount) {
    const auto sourceIt = std::find_if(
        scene.getObjects().begin(),
        scene.getObjects().end(),
        [](const std::shared_ptr<Object>& object) {
            return object && object->getName() == "Taffy";
        });
    if (sourceIt == scene.getObjects().end() || !*sourceIt) {
        LOG_WARN("Stress test: source object 'Taffy' was not found");
        return false;
    }
    if (countTaffyStressInstances(scene) != 0) {
        return false;
    }

    const auto source = *sourceIt;
    const auto mesh = source->getMesh();
    if (!mesh || mesh->getVertices().empty()) {
        LOG_WARN("Stress test: Taffy has no mesh data");
        return false;
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
    if (instanceCount != GridSide * GridSide * GridSide) {
        return false;
    }

    const TSVec3f instanceScale = source->getScale() * InstanceScale;
    const TSVec3f localCenter = (boundsMin + boundsMax) * 0.5f;
    const float firstCell =
        -0.5f * static_cast<float>(GridSide - 1) * GridSpacing;

    for (size_t index = 0; index < instanceCount; ++index) {
        const uint32_t x = static_cast<uint32_t>(index % GridSide);
        const uint32_t z = static_cast<uint32_t>(
            (index / GridSide) % GridSide);
        const uint32_t y = static_cast<uint32_t>(
            index / (GridSide * GridSide));

        // Object::clone preserves renderId for immutable scene snapshots.
        // Runtime instances need fresh identities, so copy render properties
        // into newly created Objects instead.
        auto instance = Object::create(
            std::string(TaffyStressPrefix) + std::to_string(index));
        instance->setMesh(mesh);
        instance->setMaterial(source->getMaterial());
        instance->setRotation(source->getRotationQuat());
        instance->setScale(instanceScale);
        instance->setActive(source->isActive());
        instance->setFlipProjectionY(source->getFlipProjectionY());
        instance->setPosition(TSVec3f(
            firstCell + static_cast<float>(x) * GridSpacing -
                localCenter.x * instanceScale.x,
            static_cast<float>(y) * GridSpacing -
                boundsMin.y * instanceScale.y,
            firstCell + static_cast<float>(z) * GridSpacing -
                localCenter.z * instanceScale.z));
        scene.addObject(std::move(instance));
    }

    LOG_INFO("Stress test: generated {} Taffy instances", instanceCount);
    return true;
}

size_t removeTaffyStressInstances(Scene& scene) {
    const size_t removed = scene.removeObjectsIf(
        [](const Object& object) {
            return object.getName().starts_with(TaffyStressPrefix);
        });
    if (removed != 0) {
        LOG_INFO("Stress test: removed {} Taffy instances", removed);
    }
    return removed;
}

Object* findTaffyRotationAnchor(Scene& scene) {
    if (auto* source = scene.findObject("Taffy")) {
        return source;
    }
    return scene.findObject(
        std::string(TaffyStressPrefix) + "0");
}

bool applyTaffyRotation(
    Scene& scene,
    const TSVec3f& baseRotation,
    float yawOffset) {
    bool updated = false;
    const TSVec3f rotation =
        baseRotation + TSVec3f(0.0f, yawOffset, 0.0f);
    for (const auto& object : scene.getObjects()) {
        if (!object) continue;
        if (object->getName() == "Taffy" ||
            object->getName().starts_with(TaffyStressPrefix)) {
            object->setRotation(rotation);
            updated = true;
        }
    }
    return updated;
}

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

const char* materialCategoryName(MaterialCategory category) {
    switch (category) {
    case MaterialCategory::Scene: return "Scene";
    case MaterialCategory::Vegetation: return "Vegetation";
    case MaterialCategory::Character: return "Character";
    case MaterialCategory::Special: return "Special";
    }
    return "Unknown";
}

SceneChange drawMaterialDebug(Material& material) {
    SceneChange changes = SceneChange::None;
    const auto descriptor = material.getDescriptor();
    if (descriptor) {
        ImGui::Text("Material: %s", descriptor->getName().c_str());
        ImGui::TextDisabled(
            "Descriptor: %s",
            descriptor->getSourcePath().generic_string().c_str());
        if (const auto technique = material.getTechnique()) {
            ImGui::Text(
                "Technique: %s (%s)",
                technique->getName().c_str(),
                materialCategoryName(technique->getCategory()));
        }
        ImGui::Separator();
        for (const auto& property : descriptor->getProperties()) {
            ImGui::PushID(property.name.c_str());
            switch (property.type) {
            case MaterialPropertyType::Float: {
                float value = material.getFloat(property.name);
                if (ImGui::DragFloat(property.name.c_str(), &value, 0.01f)) {
                    material.setFloat(property.name, value);
                    changes |= SceneChange::Material;
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
                    changes |= SceneChange::Material;
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
                    changes |= SceneChange::Material;
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
        changes |= SceneChange::Material | SceneChange::Structural;
    }

    bool castsShadows = material.castsShadows();
    if (ImGui::Checkbox("Cast Shadows", &castsShadows)) {
        material.setCastShadows(castsShadows);
        changes |= SceneChange::Material | SceneChange::Structural;
    }

    float alphaCutoff = material.getAlphaCutoff();
    if (ImGui::DragFloat("Alpha Cutoff", &alphaCutoff, 0.01f, 0.0f, 1.0f)) {
        material.setAlphaCutoff(alphaCutoff);
        changes |= SceneChange::Material;
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
                        changes |= SceneChange::Material | SceneChange::Structural;
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
                    changes |= SceneChange::Material | SceneChange::Structural;
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
                bool generateMipmaps = binding.generateMipmaps;
                if (ImGui::Checkbox("Generate Mipmaps", &generateMipmaps)) {
                    material.setTextureMipmaps(slot, generateMipmaps);
                    // Mip count is part of the physical image description, so
                    // changing it requires rebuilding the cached GPU texture.
                    changes |= SceneChange::Material |
                        SceneChange::Structural;
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
                    changes |= SceneChange::Material;
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
    return changes;
}

SceneChange drawObjectDebug(const std::shared_ptr<Object>& object) {
    if (!object) {
        return SceneChange::None;
    }

    SceneChange changes = SceneChange::None;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::TreeNodeEx(object.get(), flags, "%s", object->getName().c_str());
    if (!open) {
        return SceneChange::None;
    }

    bool active = object->isActive();
    if (ImGui::Checkbox("Active", &active)) {
        object->setActive(active);
        changes |= SceneChange::Structural;
    }

    bool flipProjectionY = object->getFlipProjectionY();
    if (ImGui::Checkbox("Flip Projection Y", &flipProjectionY)) {
        object->setFlipProjectionY(flipProjectionY);
        changes |= SceneChange::Transform;
    }
    ImGui::TextDisabled(
        "Front face: %s",
        flipProjectionY ? "Counter-Clockwise" : "Clockwise");

    TSVec3f position = object->getPosition();
    TSVec3f rotation = object->getRotationEuler();
    TSVec3f scale = object->getScale();
    if (drawVec3Control("Position", position)) {
        object->setPosition(position);
        changes |= SceneChange::Transform;
    }
    if (drawEulerDegreesControl("Rotation", rotation)) {
        object->setRotation(rotation);
        changes |= SceneChange::Transform;
    }
    if (drawVec3Control("Scale", scale, 0.01f)) {
        object->setScale(scale);
        changes |= SceneChange::Transform;
    }

    const auto mesh = object->getMesh();
    if (mesh) {
        ImGui::Text(
            "Mesh: %zu vertices, %zu indices, %zu LODs",
            mesh->getVertexCount(),
            mesh->getIndexCount(),
            mesh->getLODCount());
        if (mesh->getLODCount() > 1 && ImGui::TreeNode("LOD Chain")) {
            for (size_t index = 0; index < mesh->getLODCount(); ++index) {
                const auto& lod = mesh->getLOD(index);
                ImGui::Text(
                    "LOD%zu: %u triangles, coverage %.3f, error %.5f",
                    index,
                    lod.indexCount / 3,
                    lod.minimumScreenCoverage,
                    lod.simplificationError);
            }
            ImGui::TreePop();
        }
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
                        changes |= drawMaterialDebug(*material);
                    } else {
                        ImGui::TextUnformatted("Material: none");
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        } else if (const auto material = object->getMaterial()) {
            changes |= drawMaterialDebug(*material);
        } else {
            ImGui::TextUnformatted("Material: none");
        }
        ImGui::TreePop();
    }

    for (const auto& child : object->getChildren()) {
        changes |= drawObjectDebug(child);
    }

    ImGui::TreePop();
    return changes;
}

} // namespace

RendererDebugUI::RendererDebugUI(
    RenderScene& renderScene,
    SceneRendererComponents& components)
    : renderScene_(renderScene),
      components_(components) {}

void RendererDebugUI::refreshExecutionSnapshot() {
    ExecutionSnapshot snapshot;
    auto& executor = components_.rhi.frameExecutor;
    snapshot.passCount = executor.compiledPipeline().size();
    snapshot.textureCount = executor.textures().size();
    snapshot.allocatedBytes = executor.allocatedBytes();
    snapshot.passes.reserve(snapshot.passCount);
    for (const auto& pass : executor.compiledPipeline().passes()) {
        if (!pass.uniformBuffers.empty() && pass.uniformBuffers.front()) {
            snapshot.uniformPerFrameBytes +=
                pass.uniformBuffers.front()->getSize();
        }
        for (const auto& buffer : pass.uniformBuffers) {
            if (buffer) snapshot.uniformResidentBytes += buffer->getSize();
        }
        if (!pass.framePass) continue;
        PassSnapshot value;
        value.name = pass.framePass->getName();
        value.objectCount = pass.framePass->getObjectIds().size();
        value.usesSwapchain = pass.usesSwapchain;
        if (!pass.rhiPasses.empty() && pass.rhiPasses.front()) {
            const auto& desc = pass.rhiPasses.front()->getDesc();
            for (const auto& color : desc.colorAttachments) {
                if (color.image) value.attachments.push_back({color.name, false});
            }
            if (desc.depthAttachment && desc.depthAttachment->image) {
                value.attachments.push_back(
                    {desc.depthAttachment->name, true});
            }
        }
        snapshot.passes.push_back(std::move(value));
    }
    executionSnapshotSource_ = std::move(snapshot);
}

void RendererDebugUI::publishRuntimeSnapshot() {
    RuntimeSnapshot snapshot;
    const auto& state = components_;
    snapshot.settings = state.settings;
    snapshot.execution = executionSnapshotSource_;
    snapshot.deferredDeletionCount = state.rhi.device
        ? state.rhi.device->getDeferredDeletionCount()
        : 0;
    snapshot.gpuPassTimings = state.gpuPassTimings;
    snapshot.meshBufferBytes = state.sceneResources.meshBufferBytes();
    snapshot.skyboxBufferBytes = state.sceneResources.skyboxBufferBytes();
    snapshot.meshCount = state.sceneResources.meshCount();
    snapshot.materialTextureCount =
        state.sceneResources.materialTextureCount();
    snapshot.renderGraphValid =
        state.frameOrchestrator.renderGraph().isValid();
    const auto& framePacket = state.frameOrchestrator.framePacket();
    snapshot.framePassNames.reserve(framePacket.passes.size());
    for (const auto& pass : framePacket.passes) {
        snapshot.framePassNames.push_back(pass.name);
        snapshot.frameDrawCount += pass.draws.size();
    }
    const auto& graph = state.frameOrchestrator.renderGraph();
    snapshot.graphEdges.reserve(graph.getEdges().size());
    for (const auto& edge : graph.getEdges()) {
        if (edge.producer >= framePacket.passes.size() ||
            edge.consumer >= framePacket.passes.size()) {
            continue;
        }
        snapshot.graphEdges.push_back({
            framePacket.passes[edge.producer].name,
            framePacket.passes[edge.consumer].name,
            edge.resource,
            renderGraphHazardName(edge.hazard)});
    }
    snapshot.resourceLifetimes.reserve(
        graph.getResourceLifetimes().size());
    for (const auto& lifetime : graph.getResourceLifetimes()) {
        snapshot.resourceLifetimes.push_back({
            lifetime.resource,
            lifetime.firstUse,
            lifetime.lastUse,
            lifetime.external});
    }
    snapshot.graphDiagnostics = graph.getDiagnostics();
    const auto& executionPlan = state.frameOrchestrator.executionPlan();
    snapshot.executionPlanPassCount = executionPlan.passes.size();
    snapshot.executionPlanResourceCount = executionPlan.resources.size();
    snapshot.executionPlanDiagnosticCount =
        executionPlan.diagnostics.size();
    snapshot.internalRenderWidth = state.internalRenderWidth;
    snapshot.internalRenderHeight = state.internalRenderHeight;
    snapshot.displayWidth = state.displayWidth;
    snapshot.displayHeight = state.displayHeight;
    snapshot.historyStatus = state.viewState.historyStatus;
    snapshot.temporalFrameIndex = state.viewState.temporalFrameIndex;
    snapshot.previousJitterUv = state.viewState.previousJitterUv;
    snapshot.selectedSkyboxIndex =
        state.sceneResources.selectedSkyboxIndex();
    snapshot.activeSkyboxName = state.sceneResources.activeSkyboxName();
    for (const auto& skybox : state.sceneResources.skyboxVariants()) {
        snapshot.skyboxes.push_back({skybox.name, skybox.path});
    }

    std::scoped_lock lock(snapshotMutex_);
    const uint32_t writeIndex = 1u - publishedSnapshotIndex_;
    runtimeSnapshots_[writeIndex] = std::move(snapshot);
    publishedSnapshotIndex_ = writeIndex;
}

RendererDebugUI::RuntimeSnapshot
RendererDebugUI::readRuntimeSnapshot() const {
    std::scoped_lock lock(snapshotMutex_);
    return runtimeSnapshots_[publishedSnapshotIndex_];
}

void RendererDebugUI::submitUICommand(UICommand command) {
    std::scoped_lock lock(commandMutex_);
    if (pendingCommand_) {
        command.resetTemporalHistory = command.resetTemporalHistory ||
            pendingCommand_->resetTemporalHistory;
        command.internalExtentDirty = command.internalExtentDirty ||
            pendingCommand_->internalExtentDirty;
        if (!command.selectedSkyboxIndex) {
            command.selectedSkyboxIndex =
                pendingCommand_->selectedSkyboxIndex;
        }
    }
    pendingCommand_ = std::move(command);
}

void RendererDebugUI::consumeUICommands() {
    std::optional<UICommand> command;
    {
        std::scoped_lock lock(commandMutex_);
        command.swap(pendingCommand_);
    }
    if (!command) return;

    auto& state = components_;
    state.settings = std::move(command->settings);
    if (command->resetTemporalHistory) {
        state.viewState.temporalHistoryValid = false;
        state.viewState.previousModelMatrices.clear();
        state.frameOrchestrator.resetTemporalHistory();
    }
    if (command->internalExtentDirty) {
        state.internalExtentDirty = true;
    }
    if (command->selectedSkyboxIndex &&
        state.sceneResources.selectSkybox(*command->selectedSkyboxIndex)) {
        state.loggedSkyboxDrawState = false;
    }
}

void RendererDebugUI::draw() {
    const RuntimeSnapshot snapshot = readRuntimeSnapshot();
    if (!uiSettingsInitialized_) {
        uiSettings_ = snapshot.settings;
        uiSettingsInitialized_ = true;
    }
    bool resetTemporalHistory = false;
    bool internalExtentDirty = false;
    std::optional<int> selectedSkyboxIndex;
    const auto submitFrameCommands = [&]() {
        UICommand command;
        command.settings = uiSettings_;
        command.resetTemporalHistory = resetTemporalHistory;
        command.internalExtentDirty = internalExtentDirty;
        command.selectedSkyboxIndex = selectedSkyboxIndex;
        submitUICommand(std::move(command));
    };
    auto lockedScene = renderScene_.lock();
    const auto scene = lockedScene.scene();
    SceneChange publishedChanges = SceneChange::None;
    if (components_.resourceMonitor) {
        components_.resourceMonitor->draw(
            snapshot.deferredDeletionCount,
            snapshot.gpuPassTimings);
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Objects");
    if (!scene) {
        ImGui::TextUnformatted("No scene");
    } else {
        ImGui::Text("Scene: %s", scene->getName().c_str());
        ImGui::Text("Objects: %zu", scene->getObjectCount());
        const auto now = std::chrono::steady_clock::now();
        if (!taffyRotationEnabled_) {
            if (ImGui::Button("Start Taffy Rotation")) {
                if (const auto* anchor = findTaffyRotationAnchor(*scene)) {
                    taffyRotationEnabled_ = true;
                    taffyBaseRotation_ = anchor->getRotationEuler();
                    taffyYawOffset_ = 0.0f;
                    lastTaffyRotationTime_ = now;
                }
            }
        } else if (ImGui::Button("Stop Taffy Rotation")) {
            taffyRotationEnabled_ = false;
            lastTaffyRotationTime_ = {};
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            taffyRotationEnabled_
                ? "45 degrees/second around Y"
                : "Rotation paused");

        const size_t stressInstanceCount =
            countTaffyStressInstances(*scene);
        if (stressInstanceCount == 0) {
            if (ImGui::Button("Generate 1000 Taffys")) {
                if (createTaffyStressInstances(*scene, 1000)) {
                    publishedChanges |= SceneChange::Structural;
                }
            }
        } else {
            ImGui::TextDisabled(
                "Taffy stress instances: %zu", stressInstanceCount);
            if (ImGui::Button("Remove 1000 Taffys")) {
                if (removeTaffyStressInstances(*scene) != 0) {
                    publishedChanges |= SceneChange::Structural;
                }
            }
        }

        if (taffyRotationEnabled_) {
            const float deltaSeconds = std::clamp(
                std::chrono::duration<float>(
                    now - lastTaffyRotationTime_).count(),
                0.0f,
                0.1f);
            taffyYawOffset_ = std::fmod(
                taffyYawOffset_ + pi<float>() * 0.25f * deltaSeconds,
                two_pi<float>());
            if (applyTaffyRotation(
                    *scene, taffyBaseRotation_, taffyYawOffset_)) {
                publishedChanges |= SceneChange::Transform;
            }
            lastTaffyRotationTime_ = now;
        }
        ImGui::Separator();
        for (const auto& object : scene->getObjects()) {
            if (object &&
                object->getName().starts_with(TaffyStressPrefix)) {
                continue;
            }
            publishedChanges |= drawObjectDebug(object);
        }
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Inspector");
    const float fps = ImGui::GetIO().Framerate;
    const float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

    const uint64_t meshBufferBytes = snapshot.meshBufferBytes;
    const uint64_t skyboxBufferBytes = snapshot.skyboxBufferBytes;

    const uint64_t uniformResidentBytes =
        snapshot.execution.uniformResidentBytes;
    const uint64_t uniformPerFrameBytes =
        snapshot.execution.uniformPerFrameBytes;
    const uint64_t uniformBytesPerSecond =
        static_cast<uint64_t>(static_cast<double>(uniformPerFrameBytes) * static_cast<double>(fps));

    ImGui::Text("Tasrovy RHI frame");
    ImGui::Text("FPS: %.1f  Frame: %.2f ms", fps, frameMs);
    const char* pipelineNames[] = {"PBR", "Deferred", "Stylized PBR"};
    if (lockedScene.pipeline()) {
        if (lockedScene.pipeline()->getName() == "Deferred") {
            uiSettings_.selectedPipelineIndex = 1;
        } else if (lockedScene.pipeline()->getName() == "PBR") {
            uiSettings_.selectedPipelineIndex = 0;
        } else if (lockedScene.pipeline()->getName() == "StylizedPBR") {
            uiSettings_.selectedPipelineIndex = 2;
        }
    }
    if (ImGui::Combo("Pipeline", &uiSettings_.selectedPipelineIndex, pipelineNames, 3)) {
        if (uiSettings_.selectedPipelineIndex == 2) {
            lockedScene.pipeline() = StylizedPBRPipeline::create();
        } else if (uiSettings_.selectedPipelineIndex == 1) {
            lockedScene.pipeline() = DeferredPipeline::create();
        } else {
            lockedScene.pipeline() = PBRPipeline::create();
        }
        uiSettings_.debugOutputResource.clear();
        uiSettings_.debugOutputSemantic = DebugTextureSemantic::FinalOutput;
        publishedChanges |= SceneChange::Pipeline;
        LOG_INFO(
            "SceneRenderer: switched pipeline to '{}'",
            lockedScene.pipeline()->getName());
    }
    ImGui::Text("Passes: %zu", snapshot.execution.passCount);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Render Graph: %s",
        snapshot.renderGraphValid ? "valid" : "invalid");
    if (ImGui::CollapsingHeader("Render Graph")) {
        for (size_t index = 0; index < snapshot.framePassNames.size(); ++index) {
            ImGui::Text(
                "%zu. %s",
                index,
                snapshot.framePassNames[index].c_str());
        }
        if (ImGui::TreeNode("Dependencies")) {
            for (const auto& edge : snapshot.graphEdges) {
                ImGui::BulletText(
                    "%s -> %s  [%s: %s]",
                    edge.producer.c_str(),
                    edge.consumer.c_str(),
                    edge.hazard.c_str(),
                    edge.resource.c_str());
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Resource Lifetimes")) {
            for (const auto& lifetime : snapshot.resourceLifetimes) {
                ImGui::BulletText(
                    "%s  [%zu, %zu]%s",
                    lifetime.resource.c_str(),
                    lifetime.firstUse,
                    lifetime.lastUse,
                    lifetime.external ? " external" : "");
            }
            ImGui::TreePop();
        }
        for (const auto& diagnostic : snapshot.graphDiagnostics) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                "%s",
                diagnostic.c_str());
        }
    }
    ImGui::Text("Meshes: %zu", snapshot.meshCount);
    ImGui::Text(
        "Render Textures: %zu",
        snapshot.execution.textureCount);
    ImGui::Text(
        "Material Textures: %zu",
        snapshot.materialTextureCount);
    if (ImGui::CollapsingHeader("Data Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Uniform/frame: %s", formatBytes(uniformPerFrameBytes).c_str());
        ImGui::Text("Uniform/sec: %s/s", formatBytes(uniformBytesPerSecond).c_str());
        ImGui::Text("Uniform resident: %s", formatBytes(uniformResidentBytes).c_str());
        ImGui::Text("Mesh buffers: %s", formatBytes(meshBufferBytes).c_str());
        ImGui::Text("Skybox buffers: %s", formatBytes(skyboxBufferBytes).c_str());
        ImGui::Text(
            "Render textures: %s",
            formatBytes(snapshot.execution.allocatedBytes).c_str());
        ImGui::Text(
            "Skybox variants: %zu",
            snapshot.skyboxes.size());
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* shadowTechniques[] = {
            "Shadow Map",
            "Cascaded Shadow Maps",
            "Virtual Shadow Maps (resident pages)"
        };
        ImGui::Combo(
            "Technique",
            &uiSettings_.shadowTechnique,
            shadowTechniques,
            IM_ARRAYSIZE(shadowTechniques));
        ImGui::SliderFloat("Slope Bias", &uiSettings_.shadowSlopeBias, 0.0f, 0.02f, "%.5f");
        ImGui::SliderFloat("Minimum Bias", &uiSettings_.shadowMinimumBias, 0.0f, 0.01f, "%.5f");
        ImGui::SliderFloat("Strength", &uiSettings_.shadowStrength, 0.0f, 1.0f);
        ImGui::SliderFloat(
            "CSM Distance", &uiSettings_.csmMaximumDistance, 5.0f, 150.0f, "%.1f");
        if (uiSettings_.shadowTechnique != static_cast<int>(ShadowTechnique::ShadowMap)) {
            ImGui::SliderFloat(
                "CSM Split Lambda", &uiSettings_.csmSplitLambda, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat(
                "CSM Blend", &uiSettings_.csmBlendFraction, 0.0f, 0.30f, "%.2f");
            if (uiSettings_.shadowTechnique ==
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
        ImGui::Checkbox("Adaptive PCSS", &uiSettings_.pcssEnabled);
        if (uiSettings_.pcssEnabled) {
            ImGui::SliderFloat("PCSS Light Size", &uiSettings_.pcssLightSize, 0.001f, 0.08f, "%.4f");
            ImGui::SliderFloat(
                "PCSS Max Radius", &uiSettings_.pcssMaxFilterRadius, 0.002f, 0.12f, "%.4f");
        }

        ImGui::Checkbox("HBAO", &uiSettings_.ssaoEnabled);
        if (uiSettings_.ssaoEnabled) {
            ImGui::SliderFloat("HBAO Screen Radius", &uiSettings_.ssaoRadiusPixels, 2.0f, 64.0f);
            ImGui::SliderFloat("HBAO World Radius", &uiSettings_.ssaoWorldRadius, 0.05f, 5.0f);
            ImGui::SliderFloat("HBAO Intensity", &uiSettings_.ssaoIntensity, 0.0f, 4.0f);
            ImGui::SliderFloat("HBAO Bias", &uiSettings_.ssaoBias, 0.0f, 0.2f, "%.4f");
        }

        if (ImGui::Checkbox("SSR", &uiSettings_.ssrEnabled)) {
            // SSR is part of the temporal input, so toggling it changes the
            // history's meaning and requires a fresh accumulation.
            resetTemporalHistory = true;
        }
        if (uiSettings_.ssrEnabled) {
            ImGui::SliderFloat("SSR Max Distance", &uiSettings_.ssrMaxDistance, 0.5f, 30.0f);
            ImGui::SliderFloat("SSR Step Size", &uiSettings_.ssrStepSize, 0.02f, 1.0f);
            ImGui::SliderFloat("SSR Thickness", &uiSettings_.ssrThickness, 0.01f, 1.0f);
            ImGui::SliderFloat("SSR Intensity", &uiSettings_.ssrIntensity, 0.0f, 1.0f);
        }
    }

    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* temporalModes[] = {
            "Off (Spatial Upscale)",
            "TAA (Native Resolution)",
            "TAAU (Display Resolution)"
        };
        if (ImGui::Combo(
                "Temporal AA", &uiSettings_.temporalAAMode,
                temporalModes, static_cast<int>(std::size(temporalModes)))) {
            resetTemporalHistory = true;
            // Native TAA renders the entire deferred graph at the display
            // extent; Off and TAAU restore the configured fixed internal size.
            internalExtentDirty = true;
        }
        if (uiSettings_.temporalAAMode != 0) {
            ImGui::SliderFloat(
                "History Weight", &uiSettings_.taaHistoryWeight, 0.0f, 0.98f);
        }
        if (uiSettings_.temporalAAMode == 2) {
            if (ImGui::SliderFloat(
                    "Mip Bias Adjustment",
                    &uiSettings_.temporalMipBiasAdjustment,
                    -2.0f,
                    2.0f,
                    "%+.2f")) {
                resetTemporalHistory = true;
            }
            const float internalScale = std::min(
                static_cast<float>(snapshot.internalRenderWidth) /
                    static_cast<float>(std::max(snapshot.displayWidth, 1u)),
                static_cast<float>(snapshot.internalRenderHeight) /
                    static_cast<float>(std::max(snapshot.displayHeight, 1u)));
            const float automaticMipBias = std::clamp(
                std::log2(std::max(internalScale, 0.25f)),
                -2.0f,
                0.0f);
            const float effectiveMipBias = std::clamp(
                automaticMipBias + uiSettings_.temporalMipBiasAdjustment,
                -2.0f,
                0.0f);
            ImGui::TextDisabled(
                "Mip Bias: auto %.2f, effective %.2f",
                automaticMipBias,
                effectiveMipBias);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##MipBias")) {
                uiSettings_.temporalMipBiasAdjustment = 0.0f;
                resetTemporalHistory = true;
            }
        }
        if (ImGui::SliderFloat(
                "Internal Resolution",
                &uiSettings_.internalResolutionPercent,
                25.0f,
                200.0f,
                "%.0f%%")) {
            // The setting is dormant in Native TAA and is applied when Off or
            // TAAU is selected again.
            internalExtentDirty = uiSettings_.temporalAAMode != 1;
            resetTemporalHistory = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("100%")) {
            uiSettings_.internalResolutionPercent = 100.0f;
            internalExtentDirty = uiSettings_.temporalAAMode != 1;
            resetTemporalHistory = true;
        }
        ImGui::Text(
            "Internal: %u x %u  Display: %u x %u",
            snapshot.internalRenderWidth,
            snapshot.internalRenderHeight,
            snapshot.displayWidth,
            snapshot.displayHeight);
        ImGui::Text("Requested internal scale: %.0f%%", uiSettings_.internalResolutionPercent);
        ImGui::Text(
            "History: %s  frame: %llu",
            snapshot.historyStatus.c_str(),
            static_cast<unsigned long long>(snapshot.temporalFrameIndex));
        ImGui::TextDisabled(
            "Previous jitter: %.6f, %.6f",
            snapshot.previousJitterUv.x,
            snapshot.previousJitterUv.y);
        if (uiSettings_.temporalAAMode == 1) {
            ImGui::TextDisabled(
                "Native TAA forces 100%%; the requested scale is retained for TAAU/Off.");
        }
        ImGui::Separator();
        bool dofChanged = ImGui::Checkbox(
            "Depth of Field (Pre-TAA)", &uiSettings_.depthOfFieldEnabled);
        if (uiSettings_.depthOfFieldEnabled) {
            dofChanged |= ImGui::DragFloat(
                "Focus Distance", &uiSettings_.dofFocusDistance,
                0.05f, 0.05f, 100.0f, "%.2f");
            dofChanged |= ImGui::DragFloat(
                "Focus Range", &uiSettings_.dofFocusRange,
                0.05f, 0.05f, 25.0f, "%.2f");
            dofChanged |= ImGui::SliderFloat(
                "DOF Max Radius", &uiSettings_.dofMaxBlurRadius,
                0.5f, 20.0f, "%.1f px");
            dofChanged |= ImGui::SliderFloat(
                "DOF Strength", &uiSettings_.dofStrength, 0.0f, 2.0f);
        }
        if (dofChanged) {
            resetTemporalHistory = true;
        }
        ImGui::Checkbox("Motion Blur", &uiSettings_.motionBlurEnabled);
        if (uiSettings_.motionBlurEnabled) {
            ImGui::SliderFloat(
                "Motion Strength", &uiSettings_.motionBlurStrength, 0.0f, 2.0f);
            ImGui::SliderFloat(
                "Motion Max Radius", &uiSettings_.motionBlurMaxRadius,
                1.0f, 64.0f, "%.1f px");
            ImGui::SliderInt(
                "Motion Samples", &uiSettings_.motionBlurSamples, 4, 16);
        }
        ImGui::Separator();
        ImGui::Checkbox("Bloom", &uiSettings_.bloomEnabled);
        ImGui::SliderFloat("Bloom Threshold", &uiSettings_.bloomThreshold, 0.0f, 10.0f);
        ImGui::SliderFloat("Bloom Intensity", &uiSettings_.bloomIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Bloom Radius", &uiSettings_.bloomRadius, 0.25f, 4.0f);
        ImGui::SliderFloat("Exposure", &uiSettings_.exposure, 0.05f, 5.0f);
        ImGui::Checkbox(
            "Color Grading LUT", &uiSettings_.colorGradingEnabled);
        if (uiSettings_.colorGradingEnabled) {
            ImGui::SliderFloat(
                "Color Grading Strength",
                &uiSettings_.colorGradingStrength,
                0.0f,
                1.0f);
            ImGui::SliderFloat(
                "LUT Exposure Compensation",
                &uiSettings_.colorGradingExposureCompensationEv,
                -4.0f,
                4.0f,
                "%+.2f EV");
        }
        ImGui::Checkbox(
            "Final CAS Sharpening",
            &uiSettings_.finalSharpeningEnabled);
        if (uiSettings_.finalSharpeningEnabled) {
            ImGui::SliderFloat(
                "Final Sharpening Strength",
                &uiSettings_.finalSharpeningStrength,
                0.0f,
                1.0f);
        }
        ImGui::SliderFloat(
            "Chromatic Aberration",
            &uiSettings_.chromaticAberrationPixels,
            0.0f,
            3.0f,
            "%.2f px");
        ImGui::SliderFloat(
            "Vignette Strength",
            &uiSettings_.vignetteStrength,
            0.0f,
            1.0f);
        if (uiSettings_.vignetteStrength > 0.0f) {
            ImGui::SliderFloat(
                "Vignette Power",
                &uiSettings_.vignettePower,
                0.25f,
                8.0f);
            float vignetteColor[3] = {
                uiSettings_.vignetteColor.x,
                uiSettings_.vignetteColor.y,
                uiSettings_.vignetteColor.z
            };
            if (ImGui::ColorEdit3("Vignette Color", vignetteColor)) {
                uiSettings_.vignetteColor = TSVec3f(
                    vignetteColor[0], vignetteColor[1], vignetteColor[2]);
            }
        }
        ImGui::SliderFloat(
            "Display Dither Strength",
            &uiSettings_.displayDitherStrength,
            0.0f,
            1.0f);
        ImGui::Separator();
        bool dlssNrChanged = ImGui::Checkbox(
            "DLSS Neural Rendering (Experimental)",
            &uiSettings_.dlssNeuralRenderingEnabled);
        if (uiSettings_.dlssNeuralRenderingEnabled) {
            static const char* dlssNrStyles[] = {
                "Default", "Natural", "Cinematic"
            };
            dlssNrChanged |= ImGui::Combo(
                "NR Style",
                &uiSettings_.dlssNrStyle,
                dlssNrStyles,
                static_cast<int>(std::size(dlssNrStyles)));
            dlssNrChanged |= ImGui::SliderFloat(
                "NR Intensity", &uiSettings_.dlssNrIntensity,
                0.0f, 1.0f);
            dlssNrChanged |= ImGui::SliderFloat(
                "NR Local Tone", &uiSettings_.dlssNrLocalToneStrength,
                0.0f, 1.0f);
            dlssNrChanged |= ImGui::SliderFloat(
                "NR Local Structure",
                &uiSettings_.dlssNrLocalStructureStrength,
                0.0f, 1.0f);
            dlssNrChanged |= ImGui::SliderFloat(
                "NR Skin Structure",
                &uiSettings_.dlssNrSkinStructureStrength,
                -1.0f, 2.0f);
            dlssNrChanged |= ImGui::Checkbox(
                "NR Automatic Mask", &uiSettings_.dlssNrUseAutoMask);
            dlssNrChanged |= ImGui::Checkbox(
                "NR UI Correction", &uiSettings_.dlssNrUiCorrection);
            ImGui::TextDisabled(
                "Feature 18 backend pending; current pass preserves SDR color.");
        }
        if (dlssNrChanged) {
            resetTemporalHistory = true;
        }
        ImGui::Separator();
        ImGui::Checkbox("Normal Outline", &uiSettings_.outlineEnabled);
        if (uiSettings_.outlineEnabled) {
            ImGui::SliderFloat("Outline Threshold", &uiSettings_.outlineThreshold, 0.001f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &uiSettings_.outlineThickness, 0.5f, 5.0f);
            ImGui::SliderFloat("Outline Strength", &uiSettings_.outlineStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Softness", &uiSettings_.outlineSoftness, 0.001f, 0.5f);
            float outlineColor[3] = {
                uiSettings_.outlineColor.x, uiSettings_.outlineColor.y, uiSettings_.outlineColor.z
            };
            if (ImGui::ColorEdit3("Outline Color", outlineColor)) {
                uiSettings_.outlineColor = TSVec3f(
                    outlineColor[0], outlineColor[1], outlineColor[2]);
            }
        }
        ImGui::Checkbox(
            "Temporal Outline Denoise",
            &uiSettings_.outlineTemporalDenoise);
        if (uiSettings_.outlineTemporalDenoise) {
            ImGui::SliderFloat(
                "Outline History Weight",
                &uiSettings_.outlineHistoryWeight,
                0.0f,
                0.98f,
                "%.2f");
        }
    }

    if (ImGui::CollapsingHeader("Debug Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(
            "Frame packet: %zu passes, %zu draws",
            snapshot.framePassNames.size(),
            snapshot.frameDrawCount);
        ImGui::TextDisabled(
            "RHI plan: %zu passes, %zu resources, %zu diagnostics",
            snapshot.executionPlanPassCount,
            snapshot.executionPlanResourceCount,
            snapshot.executionPlanDiagnosticCount);

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

        for (const auto& pass : snapshot.execution.passes) {
            uint32_t colorIndex = 0;
            for (const auto& attachment : pass.attachments) {
                if (!attachment.depth) {
                    const auto semantic =
                        classifyColorResource(attachment.name);
                    options.push_back({
                        pass.name + " / Color" +
                            std::to_string(colorIndex) + " / " + attachment.name +
                            " [" + semanticName(semantic) + "]",
                        attachment.name,
                        semantic
                    });
                    ++colorIndex;
                }
            }

            const auto depth = std::find_if(
                pass.attachments.begin(), pass.attachments.end(),
                [](const AttachmentSnapshot& value) { return value.depth; });
            if (depth != pass.attachments.end()) {
                const auto semantic = depth->name == "SceneDepth"
                    ? DebugTextureSemantic::SceneLinearDepth
                    : DebugTextureSemantic::RawDepth;
                options.push_back({
                    pass.name + " / Depth / " + depth->name +
                        " [" + semanticName(semantic) + "]",
                    depth->name,
                    semantic
                });
            }
        }

        int currentOption = 0;
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            if (options[static_cast<size_t>(i)].resource ==
                    uiSettings_.debugOutputResource &&
                options[static_cast<size_t>(i)].semantic ==
                    uiSettings_.debugOutputSemantic) {
                currentOption = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Display", options[static_cast<size_t>(currentOption)].label.c_str())) {
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                const bool selected = i == currentOption;
                if (ImGui::Selectable(options[static_cast<size_t>(i)].label.c_str(), selected)) {
                    uiSettings_.debugOutputResource = options[static_cast<size_t>(i)].resource;
                    uiSettings_.debugOutputSemantic =
                        options[static_cast<size_t>(i)].semantic;
                    LOG_INFO(
                        "SceneRenderer: debug output '{}'",
                        uiSettings_.debugOutputResource.empty()
                            ? std::string("Final Output")
                            : uiSettings_.debugOutputResource);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (uiSettings_.debugOutputSemantic ==
            DebugTextureSemantic::Velocity) {
            ImGui::SliderFloat(
                "Velocity Preview Scale",
                &uiSettings_.debugVelocityScale,
                1.0f,
                256.0f,
                "%.1f");
        }
        if (uiSettings_.debugOutputSemantic ==
                DebugTextureSemantic::SceneLinearDepth ||
            uiSettings_.debugOutputSemantic ==
                DebugTextureSemantic::HiZLinearDepth) {
            ImGui::SliderFloat(
                "Depth Preview Range",
                &uiSettings_.debugDepthRange,
                1.0f,
                500.0f,
                "%.1f");
        }
    }
    ImGui::Separator();

    if (!scene) {
        ImGui::TextUnformatted("No scene");
        ImGui::End();
        submitFrameCommands();
        return;
    }

    ImGui::Text("Scene: %s", scene->getName().c_str());
    ImGui::Text(
        "Objects: %zu  Cameras: %zu  Lights: %zu",
        scene->getObjectCount(),
        scene->getCameraCount(),
        scene->getLightCount());

    if (ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& skyboxVariants = snapshot.skyboxes;
        if (skyboxVariants.empty()) {
            ImGui::TextUnformatted("No skybox variants");
        } else {
            const int currentSkyboxIndex = std::clamp(
                snapshot.selectedSkyboxIndex,
                0,
                static_cast<int>(skyboxVariants.size()) - 1);
            const auto& current =
                skyboxVariants[static_cast<size_t>(currentSkyboxIndex)];
            if (ImGui::BeginCombo("Environment", current.name.c_str())) {
                for (int i = 0; i < static_cast<int>(skyboxVariants.size()); ++i) {
                    const bool selected = i == currentSkyboxIndex;
                    if (ImGui::Selectable(
                            skyboxVariants[static_cast<size_t>(i)].name.c_str(),
                            selected)) {
                        selectedSkyboxIndex = i;
                        LOG_INFO(
                            "SceneRenderer: switched skybox to '{}'",
                            skyboxVariants[static_cast<size_t>(i)].name);
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
                publishedChanges |= SceneChange::Transform;
            }
            if (drawEulerDegreesControl("Rotation##Camera", rotation)) {
                camera->setRotation(rotation);
                publishedChanges |= SceneChange::Transform;
            }
            if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f)) {
                camera->setFOV(fov);
                publishedChanges |= SceneChange::Transform;
            }
            if (ImGui::DragFloat("Aspect", &aspect, 0.01f, 0.1f, 4.0f)) {
                camera->setAspect(aspect);
                publishedChanges |= SceneChange::Transform;
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
                    publishedChanges |= SceneChange::Lighting;
                }
                if (drawColorControl("Color", color)) {
                    light->setColor(color);
                    publishedChanges |= SceneChange::Lighting;
                }
                if (ImGui::DragFloat("Intensity", &intensity, 0.1f, 0.0f, 1000.0f)) {
                    light->setIntensity(intensity);
                    publishedChanges |= SceneChange::Lighting;
                }

                if (auto* point = dynamic_cast<PointLight*>(light)) {
                    TSVec3f position = point->getPosition();
                    float constant = point->getConstant();
                    float linear = point->getLinear();
                    float quadratic = point->getQuadratic();
                    if (drawVec3Control("Position", position)) {
                        point->setPosition(position);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Constant", &constant, 0.01f, 0.0f, 10.0f)) {
                        point->setConstant(constant);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Linear", &linear, 0.01f, 0.0f, 10.0f)) {
                        point->setLinear(linear);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Quadratic", &quadratic, 0.01f, 0.0f, 10.0f)) {
                        point->setQuadratic(quadratic);
                        publishedChanges |= SceneChange::Lighting;
                    }
                }

                if (auto* area = dynamic_cast<AreaLight*>(light)) {
                    TSVec3f position = area->getPosition();
                    float width = area->getWidth();
                    float height = area->getHeight();
                    bool twoSided = area->isTwoSided();
                    if (drawVec3Control("Position", position)) {
                        area->setPosition(position);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Width", &width, 0.05f, 0.01f, 100.0f)) {
                        area->setWidth(width);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Height", &height, 0.05f, 0.01f, 100.0f)) {
                        area->setHeight(height);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::Checkbox("Two Sided", &twoSided)) {
                        area->setTwoSided(twoSided);
                        publishedChanges |= SceneChange::Lighting;
                    }
                }

                if (auto* spot = dynamic_cast<SpotLight*>(light)) {
                    TSVec3f position = spot->getPosition();
                    float cutoff = spot->getCutoff();
                    if (drawVec3Control("Position", position)) {
                        spot->setPosition(position);
                        publishedChanges |= SceneChange::Lighting;
                    }
                    if (ImGui::DragFloat("Cutoff", &cutoff, 0.1f, 0.0f, 90.0f)) {
                        spot->setCutoff(cutoff);
                        publishedChanges |= SceneChange::Lighting;
                    }
                }

                ImGui::TreePop();
            }
            ++index;
        }
    }

    if (ImGui::CollapsingHeader("Render Passes")) {
        for (const auto& pass : snapshot.execution.passes) {
            ImGui::Text(
                "%s  objects %zu  swapchain %d",
                pass.name.c_str(),
                pass.objectCount,
                pass.usesSwapchain ? 1 : 0);
        }
    }

    ImGui::End();
    lockedScene.markChanged(publishedChanges);
    submitFrameCommands();
}


} // namespace Tasrovy::Renderer
