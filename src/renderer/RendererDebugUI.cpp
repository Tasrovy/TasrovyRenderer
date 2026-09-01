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
        flipProjectionY ? "Counter-Clockwise" : "Clockwise");

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
    executionSnapshot_ = std::move(snapshot);
}

void RendererDebugUI::draw() {
    auto& state = components_;
    auto lockedScene = renderScene_.lock();
    const auto scene = lockedScene.scene();
    if (state.resourceMonitor) {
        state.resourceMonitor->draw(
            state.rhi.device
                ? state.rhi.device->getDeferredDeletionCount()
                : 0,
            state.gpuPassTimings);
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Objects");
    if (!scene) {
        ImGui::TextUnformatted("No scene");
    } else {
        ImGui::Text("Scene: %s", scene->getName().c_str());
        ImGui::Text("Objects: %zu", scene->getObjectCount());
        ImGui::Separator();
        bool needsPipelineRefresh = false;
        for (const auto& object : scene->getObjects()) {
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

    const uint64_t uniformResidentBytes =
        executionSnapshot_.uniformResidentBytes;
    const uint64_t uniformPerFrameBytes =
        executionSnapshot_.uniformPerFrameBytes;
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
    ImGui::Text("Passes: %zu", executionSnapshot_.passCount);
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
        executionSnapshot_.textureCount);
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
            formatBytes(executionSnapshot_.allocatedBytes).c_str());
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
            state.displayWidth,
            state.displayHeight);
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

        for (const auto& pass : executionSnapshot_.passes) {
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
        for (const auto& pass : executionSnapshot_.passes) {
            ImGui::Text(
                "%s  objects %zu  swapchain %d",
                pass.name.c_str(),
                pass.objectCount,
                pass.usesSwapchain ? 1 : 0);
        }
    }

    ImGui::End();
}


} // namespace Tasrovy::Renderer
