#include "SceneRenderer.h"

#include "Buffer.h"
#include "CommandList.h"
#include "Device.h"
#include "Descriptor.h"
#include "Image.h"
#include "Pass.h"
#include "Pipeline.h"
#include "RHIConfig.h"
#include "SceneGeometry.h"
#include "../render/Material.h"
#include "../render/Camera.h"
#include "../render/DeferredPipeline.h"
#include "../render/Light.h"
#include "../render/Mesh.h"
#include "../render/Object.h"
#include "../render/PBRPipeline.h"
#include "../render/Pipeline.h"
#include "../render/PipelinePass.h"
#include "../render/Scene.h"
#include "../render/Shader.h"
#include "../render/Skybox.h"
#include "../render/Texture.hpp"
#include "../ui/UI.h"
#include "../window/Window.h"
#include "Logger.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Tasrovy::RHI {

using namespace Tasrovy::Render;

namespace {

struct SkyboxCandidate {
    std::string name;
    std::string path;
};

struct MeshResources {
    std::shared_ptr<Buffer> vertexBuffer;
    std::shared_ptr<Buffer> indexBuffer;
    uint32_t indexCount = 0;
};

struct UniformBufferObject {
    TSMat4f model;
    TSMat4f view;
    TSMat4f proj;
    TSVec4f lightDir;
    TSVec4f lightColor;
    TSVec4f camPosAndMetallic;
    TSVec4f roughnessAo;
    TSVec4f uvTransform;
};

struct SkyUniformBufferObject {
    TSMat4f view;
    TSMat4f proj;
};

struct PassResources {
    std::shared_ptr<PipelinePass> logicalPass;
    std::vector<std::shared_ptr<Pass>> rhiPasses;
    std::shared_ptr<Pipeline> gpuPipeline;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<DescriptorPool> descriptorPool;
    std::vector<DescriptorSet> descriptorSets;
    std::vector<std::shared_ptr<Buffer>> uniformBuffers;
    uint32_t descriptorSetsPerFrame = 1;
    bool usesSwapchain = false;
};

bool passHasExecutableWork(const PipelinePass& pass) {
    switch (pass.getExecution()) {
    case PipelinePassExecution::Fullscreen:
    case PipelinePassExecution::Skybox:
        return true;
    case PipelinePassExecution::Mesh:
        return !pass.getObjects().empty();
    case PipelinePassExecution::Compute:
    case PipelinePassExecution::UI:
        return true;
    }
    return !pass.getObjects().empty();
}

bool passUsesFullscreenDraw(const PipelinePass& pass) {
    return pass.getExecution() == PipelinePassExecution::Fullscreen;
}

bool passUsesSkyboxDraw(const PipelinePass& pass) {
    return pass.getExecution() == PipelinePassExecution::Skybox;
}

uint32_t countMeshDrawSlots(const PipelinePass& pass) {
    uint32_t count = 0;
    for (const auto& objectRef : pass.getObjects()) {
        const auto object = objectRef.lock();
        const auto mesh = object ? object->getMesh() : nullptr;
        if (!mesh) {
            continue;
        }

        const auto& submeshes = mesh->getSubmeshes();
        count += submeshes.empty() ? 1u : static_cast<uint32_t>(submeshes.size());
    }
    return std::max(1u, count);
}

RHIAttachmentLoad toRHILoad(AttachmentLoad load) {
    switch (load) {
    case AttachmentLoad::Clear:
        return RHIAttachmentLoad::Clear;
    case AttachmentLoad::Load:
        return RHIAttachmentLoad::Load;
    case AttachmentLoad::Discard:
        return RHIAttachmentLoad::Discard;
    }
    return RHIAttachmentLoad::Clear;
}

RHIAttachmentStore toRHIStore(AttachmentStore store) {
    switch (store) {
    case AttachmentStore::Store:
        return RHIAttachmentStore::Store;
    case AttachmentStore::Discard:
        return RHIAttachmentStore::Discard;
    }
    return RHIAttachmentStore::Store;
}

RenderTextureFormat toRHIFormat(PipelineTextureFormat format) {
    switch (format) {
    case PipelineTextureFormat::RGBA8Unorm:
        return RenderTextureFormat::RGBA8Unorm;
    case PipelineTextureFormat::RGBA16Float:
        return RenderTextureFormat::RGBA16Float;
    case PipelineTextureFormat::RG16Float:
        return RenderTextureFormat::RG16Float;
    case PipelineTextureFormat::Depth32Float:
        return RenderTextureFormat::Depth32Float;
    case PipelineTextureFormat::Swapchain:
        return RenderTextureFormat::Swapchain;
    }
    return RenderTextureFormat::RGBA8Unorm;
}

uint64_t bytesPerPixel(RenderTextureFormat format) {
    switch (format) {
    case RenderTextureFormat::RGBA8Unorm:
    case RenderTextureFormat::RG16Float:
    case RenderTextureFormat::Depth32Float:
    case RenderTextureFormat::Swapchain:
        return 4;
    case RenderTextureFormat::RGBA16Float:
        return 8;
    }
    return 4;
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

int selectMaterialUvMode(
    const std::string& materialName,
    int bodyMode,
    int hairMode,
    int faceMode) {
    auto lowerName = materialName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lowerName.find("hair") != std::string::npos) {
        return hairMode;
    }
    if (lowerName.find("face") != std::string::npos ||
        lowerName.find("eye") != std::string::npos) {
        return faceMode;
    }
    return bodyMode;
}

uint32_t toRHICullMode(CullMode cullMode) {
    switch (cullMode) {
    case CullMode::None:
        return CullNone;
    case CullMode::Front:
        return CullFront;
    case CullMode::Back:
        return CullBack;
    }
    return CullBack;
}

uint32_t toRHICompare(DepthTestMode mode) {
    switch (mode) {
    case DepthTestMode::Less:
        return CompareLess;
    case DepthTestMode::LessOrEqual:
        return CompareLessOrEqual;
    default:
        return CompareLess;
    }
}

uint32_t resolveTextureWidth(const PipelineTextureDesc& desc, const Device& device) {
    if (desc.extent == PipelineTextureExtent::Fixed) {
        return desc.width;
    }
    return static_cast<uint32_t>(static_cast<float>(device.getSwapchainWidth()) * desc.widthScale);
}

uint32_t resolveTextureHeight(const PipelineTextureDesc& desc, const Device& device) {
    if (desc.extent == PipelineTextureExtent::Fixed) {
        return desc.height;
    }
    return static_cast<uint32_t>(static_cast<float>(device.getSwapchainHeight()) * desc.heightScale);
}

bool isSRGB(MaterialTextureColorSpace colorSpace) {
    return colorSpace == MaterialTextureColorSpace::SRGB;
}

MaterialTextureColorSpace colorSpaceForSemantic(MaterialTextureSemantic semantic) {
    switch (semantic) {
    case MaterialTextureSemantic::BaseColor:
    case MaterialTextureSemantic::Emissive:
        return MaterialTextureColorSpace::SRGB;
    case MaterialTextureSemantic::Normal:
    case MaterialTextureSemantic::MetallicRoughnessAO:
    case MaterialTextureSemantic::Opacity:
        return MaterialTextureColorSpace::Linear;
    }
    return MaterialTextureColorSpace::Linear;
}

std::string materialTextureCacheKey(
    const std::string& path,
    MaterialTextureColorSpace colorSpace) {
    auto normalized = std::filesystem::path(path).lexically_normal().generic_string();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    normalized += isSRGB(colorSpace) ? "|srgb" : "|linear";
    return normalized;
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

const char* materialSemanticName(MaterialTextureSemantic semantic) {
    switch (semantic) {
    case MaterialTextureSemantic::BaseColor:
        return "BaseColor";
    case MaterialTextureSemantic::Normal:
        return "Normal";
    case MaterialTextureSemantic::MetallicRoughnessAO:
        return "MetallicRoughnessAO";
    case MaterialTextureSemantic::Emissive:
        return "Emissive";
    case MaterialTextureSemantic::Opacity:
        return "Opacity";
    }
    return "Unknown";
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

bool hasCubemapFaces(const std::filesystem::path& directory) {
    static const char* faces[] = {
        "right.png", "left.png", "top.png", "bottom.png", "front.png", "back.png"
    };
    for (const auto* face : faces) {
        if (!std::filesystem::exists(directory / face)) {
            return false;
        }
    }
    return true;
}

std::string normalizePathForAssets(const std::filesystem::path& path) {
    return path.generic_string();
}

std::vector<SkyboxCandidate> discoverSkyboxCandidates(const std::string& preferredPath) {
    std::vector<SkyboxCandidate> candidates;
    const std::filesystem::path resPath("res/Skyboxes");

    if (std::filesystem::exists(resPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(resPath)) {
            if (!entry.is_directory() || !hasCubemapFaces(entry.path())) {
                continue;
            }
            candidates.push_back({
                entry.path().filename().string(),
                normalizePathForAssets(entry.path())
            });
        }
    }

    if (!preferredPath.empty()) {
        const std::filesystem::path preferred(preferredPath);
        const auto preferredNormalized = normalizePathForAssets(preferred);
        const bool alreadyListed = std::any_of(
            candidates.begin(),
            candidates.end(),
            [&](const SkyboxCandidate& candidate) {
                return candidate.path == preferredNormalized;
            });
        if (!alreadyListed && hasCubemapFaces(preferred)) {
            candidates.push_back({
                preferred.filename().string(),
                preferredNormalized
            });
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const SkyboxCandidate& lhs, const SkyboxCandidate& rhs) {
            return lhs.name < rhs.name;
        });
    return candidates;
}

bool drawMaterialDebug(Material& material) {
    bool needsPipelineRefresh = false;
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
        for (const auto& [semantic, binding] : material.getSemanticTextureBindings()) {
            ImGui::Text(
                "%s  b%u  %s",
                materialSemanticName(semantic),
                binding.binding,
                binding.path.c_str());
        }
        for (const auto& [slot, binding] : material.getTextureBindings()) {
            ImGui::Text("%s  b%u  %s", slot.c_str(), binding.binding, binding.path.c_str());
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

    if (const auto mesh = object->getMesh()) {
        ImGui::Text("Mesh: %zu vertices, %zu indices", mesh->getVertexCount(), mesh->getIndexCount());
        if (ImGui::TreeNode("Submeshes")) {
            for (const auto& submesh : mesh->getSubmeshes()) {
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

    if (const auto material = object->getMaterial()) {
        if (ImGui::TreeNode("Material")) {
            needsPipelineRefresh |= drawMaterialDebug(*material);
            ImGui::TreePop();
        }
    } else {
        ImGui::TextUnformatted("Material: none");
    }

    for (const auto& child : object->getChildren()) {
        needsPipelineRefresh |= drawObjectDebug(child);
    }

    ImGui::TreePop();
    return needsPipelineRefresh;
}

} // namespace

struct SceneRenderer::RenderState {
    struct SkyboxVariant {
        std::string name;
        std::string path;
        std::shared_ptr<Image> cubemap;
    };

    std::shared_ptr<Device> device;
    std::shared_ptr<CommandList> commandList;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Image>>> renderTextures;
    std::unordered_map<std::string, std::shared_ptr<Image>> materialTextures;
    std::unordered_map<const Mesh*, MeshResources> meshes;
    std::vector<PassResources> passes;
    uint64_t renderTextureBytes = 0;
    std::shared_ptr<Image> defaultDiffuse;
    std::shared_ptr<Image> defaultNormal;
    std::shared_ptr<Image> defaultEmissive;
    std::shared_ptr<Image> defaultMsa;
    std::shared_ptr<Image> skyCubemap;
    std::vector<SkyboxVariant> skyboxVariants;
    int selectedSkyboxIndex = 0;
    std::string activeSkyboxName;
    std::shared_ptr<Buffer> skyboxVertexBuffer;
    std::shared_ptr<Buffer> skyboxIndexBuffer;
    uint32_t skyboxIndexCount = 0;
    bool loggedSkyboxDrawState = false;
    bool loggedSubmeshMaterialBindings = false;
    int selectedPipelineIndex = 0;
    std::string debugOutputResource;
    float pbrMetallic = 0.0f;
    float pbrRoughness = 1.0f;
    float pbrAo = 1.0f;
    int materialDebugMode = 0;
    int bodyUvMode = 1;
    int hairUvMode = 1;
    int faceUvMode = 1;
    float uvOffset[2] = {0.0f, 0.0f};
    float uvScale[2] = {1.0f, 1.0f};
    std::unique_ptr<Tasrovy::UI::UIOverlay> ui;
    uint32_t maxFramesInFlight = 0;

    RenderState(Tasrovy::Windowing::Window& window, uint32_t maxFrames)
        : maxFramesInFlight(maxFrames) {
        device = Device::createForWindow(window, maxFrames);
        commandList = CommandList::create();
        ui = device->createUIOverlay(window);
    }
};

void SceneRenderer::prepareSkyboxVariants(const std::string& preferredPath) {
    auto& state = *renderState_;
    auto& device = *state.device;

    if (state.skyboxVariants.empty()) {
        const auto candidates = discoverSkyboxCandidates(preferredPath);
        state.skyboxVariants.reserve(candidates.size());

        for (const auto& candidate : candidates) {
            LOG_INFO("SceneRenderer: precomputing skybox '{}' from '{}'", candidate.name, candidate.path);
            auto cubemap = device.createCubemap(candidate.path);
            device.createIBLMaps(*cubemap, candidate.name);
            state.skyboxVariants.push_back({
                candidate.name,
                candidate.path,
                std::move(cubemap)
            });
        }
    }

    if (state.skyboxVariants.empty()) {
        state.skyCubemap.reset();
        state.activeSkyboxName.clear();
        state.selectedSkyboxIndex = 0;
        return;
    }

    if (!preferredPath.empty() && state.activeSkyboxName.empty()) {
        const auto preferredNormalized = normalizePathForAssets(std::filesystem::path(preferredPath));
        for (size_t i = 0; i < state.skyboxVariants.size(); ++i) {
            if (state.skyboxVariants[i].path == preferredNormalized) {
                state.selectedSkyboxIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (state.selectedSkyboxIndex < 0 ||
        state.selectedSkyboxIndex >= static_cast<int>(state.skyboxVariants.size())) {
        state.selectedSkyboxIndex = 0;
    }

    const auto& selected = state.skyboxVariants[static_cast<size_t>(state.selectedSkyboxIndex)];
    state.skyCubemap = selected.cubemap;
    state.activeSkyboxName = selected.name;
}

SceneRenderer::SceneRenderer(Tasrovy::Windowing::Window& window, uint32_t maxFramesInFlight)
    : window_(window), maxFramesInFlight_(maxFramesInFlight) {
    renderState_ = std::make_unique<RenderState>(window_, maxFramesInFlight_);
    if (renderState_->ui) {
        renderState_->ui->setDrawCallback([this]() {
            drawSceneDebugUI();
        });
    }
    LOG_INFO("SceneRenderer: RHI initialized with {}", TASROVY_API_NAME);
}

SceneRenderer::~SceneRenderer() {
    stop();
    if (renderState_ && renderState_->device) {
        renderState_->device->waitIdle();
    }
}

void SceneRenderer::setScene(std::shared_ptr<Scene> scene) {
    std::lock_guard<std::mutex> lock(sceneMutex_);
    currentScene_ = scene ? scene->clone() : nullptr;
    renderDataDirty_ = true;
    ++renderDataDirtyVersion_;
}

void SceneRenderer::setPipeline(std::shared_ptr<PipelineBase> pipeline) {
    std::lock_guard<std::mutex> lock(sceneMutex_);
    currentPipeline_ = std::move(pipeline);
    renderDataDirty_ = true;
    ++renderDataDirtyVersion_;
}

void SceneRenderer::drawSceneDebugUI() {
    auto& state = *renderState_;
    std::lock_guard<std::mutex> lock(sceneMutex_);

    ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Inspector");
    const float fps = ImGui::GetIO().Framerate;
    const float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

    uint64_t meshBufferBytes = 0;
    for (const auto& [_, mesh] : state.meshes) {
        if (mesh.vertexBuffer) {
            meshBufferBytes += mesh.vertexBuffer->getSize();
        }
        if (mesh.indexBuffer) {
            meshBufferBytes += mesh.indexBuffer->getSize();
        }
    }

    uint64_t skyboxBufferBytes = 0;
    if (state.skyboxVertexBuffer) {
        skyboxBufferBytes += state.skyboxVertexBuffer->getSize();
    }
    if (state.skyboxIndexBuffer) {
        skyboxBufferBytes += state.skyboxIndexBuffer->getSize();
    }

    uint64_t uniformResidentBytes = 0;
    uint64_t uniformPerFrameBytes = 0;
    for (auto& pass : state.passes) {
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
    if (currentPipeline_) {
        if (currentPipeline_->getName() == "Deferred") {
            state.selectedPipelineIndex = 1;
        } else if (currentPipeline_->getName() == "PBR") {
            state.selectedPipelineIndex = 0;
        }
    }
    if (ImGui::Combo("Pipeline", &state.selectedPipelineIndex, pipelineNames, 2)) {
        currentPipeline_ = state.selectedPipelineIndex == 1
            ? std::static_pointer_cast<PipelineBase>(DeferredPipeline::create())
            : std::static_pointer_cast<PipelineBase>(PBRPipeline::create());
        state.debugOutputResource.clear();
        renderDataDirty_ = true;
        ++renderDataDirtyVersion_;
        LOG_INFO("SceneRenderer: switched pipeline to '{}'", currentPipeline_->getName());
    }
    ImGui::Text("Passes: %zu", state.passes.size());
    ImGui::Text("Meshes: %zu", state.meshes.size());
    ImGui::Text("Render Textures: %zu", state.renderTextures.size());
    ImGui::Text("Material Textures: %zu", state.materialTextures.size());
    if (ImGui::CollapsingHeader("Data Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Uniform/frame: %s", formatBytes(uniformPerFrameBytes).c_str());
        ImGui::Text("Uniform/sec: %s/s", formatBytes(uniformBytesPerSecond).c_str());
        ImGui::Text("Uniform resident: %s", formatBytes(uniformResidentBytes).c_str());
        ImGui::Text("Mesh buffers: %s", formatBytes(meshBufferBytes).c_str());
        ImGui::Text("Skybox buffers: %s", formatBytes(skyboxBufferBytes).c_str());
        ImGui::Text("Render textures: %s", formatBytes(state.renderTextureBytes).c_str());
        ImGui::Text("Skybox variants: %zu", state.skyboxVariants.size());
    }

    if (ImGui::CollapsingHeader("PBR Constants", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Metallic", &state.pbrMetallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &state.pbrRoughness, 0.0f, 1.0f);
        ImGui::SliderFloat("AO", &state.pbrAo, 0.0f, 1.0f);
        const char* materialDebugModes[] = {
            "Shaded",
            "Albedo Only",
            "Raw UV",
            "Material UV"
        };
        ImGui::Combo(
            "Material Debug",
            &state.materialDebugMode,
            materialDebugModes,
            IM_ARRAYSIZE(materialDebugModes));
        const char* uvModes[] = {
            "UV0",
            "Flip Y",
            "Flip X",
            "Flip X/Y",
            "Swap X/Y",
            "Swap + Flip Y",
            "Swap + Flip X"
        };
        const bool uvModesMatch =
            state.bodyUvMode == state.hairUvMode &&
            state.bodyUvMode == state.faceUvMode;
        const char* modelUvPreview = uvModesMatch
            ? uvModes[state.bodyUvMode]
            : "Mixed";
        if (ImGui::BeginCombo("Model UV", modelUvPreview)) {
            for (int mode = 0; mode < IM_ARRAYSIZE(uvModes); ++mode) {
                const bool selected = uvModesMatch && state.bodyUvMode == mode;
                if (ImGui::Selectable(uvModes[mode], selected)) {
                    state.bodyUvMode = mode;
                    state.hairUvMode = mode;
                    state.faceUvMode = mode;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat2("UV Offset", state.uvOffset, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::DragFloat2("UV Scale", state.uvScale, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset UV Transform")) {
            state.uvOffset[0] = 0.0f;
            state.uvOffset[1] = 0.0f;
            state.uvScale[0] = 1.0f;
            state.uvScale[1] = 1.0f;
        }
        ImGui::TextDisabled("Repeat: frac(UV * Scale + Offset)");
        if (ImGui::TreeNode("Per-Material UV Overrides")) {
            ImGui::Combo("Body UV", &state.bodyUvMode, uvModes, IM_ARRAYSIZE(uvModes));
            ImGui::Combo("Hair UV", &state.hairUvMode, uvModes, IM_ARRAYSIZE(uvModes));
            ImGui::Combo("Face UV", &state.faceUvMode, uvModes, IM_ARRAYSIZE(uvModes));
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Debug Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        struct DebugOutputOption {
            std::string label;
            std::string resource;
        };
        std::vector<DebugOutputOption> options;
        options.push_back({"Final Output", ""});

        for (const auto& pass : state.passes) {
            if (!pass.logicalPass || pass.rhiPasses.empty() || !pass.rhiPasses.front()) {
                continue;
            }

            const auto& debugPass = *pass.rhiPasses.front();
            uint32_t colorIndex = 0;
            for (const auto& attachment : debugPass.getDesc().colorAttachments) {
                if (attachment.image) {
                    options.push_back({
                        pass.logicalPass->getName() + " / Color" +
                            std::to_string(colorIndex) + " / " + attachment.name,
                        attachment.name
                    });
                }
                ++colorIndex;
            }

            if (const auto& depth = debugPass.getDesc().depthAttachment;
                depth && depth->image) {
                options.push_back({
                    pass.logicalPass->getName() + " / Depth / " + depth->name,
                    depth->name
                });
            }
        }

        int currentOption = 0;
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            if (options[static_cast<size_t>(i)].resource == state.debugOutputResource) {
                currentOption = i;
                break;
            }
        }

        if (ImGui::BeginCombo("Display", options[static_cast<size_t>(currentOption)].label.c_str())) {
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                const bool selected = i == currentOption;
                if (ImGui::Selectable(options[static_cast<size_t>(i)].label.c_str(), selected)) {
                    state.debugOutputResource = options[static_cast<size_t>(i)].resource;
                    LOG_INFO(
                        "SceneRenderer: debug output '{}'",
                        state.debugOutputResource.empty()
                            ? std::string("Final Output")
                            : state.debugOutputResource);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();

    const auto scene = currentScene_;
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
        if (state.skyboxVariants.empty()) {
            ImGui::TextUnformatted("No skybox variants");
        } else {
            if (state.selectedSkyboxIndex < 0 ||
                state.selectedSkyboxIndex >= static_cast<int>(state.skyboxVariants.size())) {
                state.selectedSkyboxIndex = 0;
            }

            const auto& current =
                state.skyboxVariants[static_cast<size_t>(state.selectedSkyboxIndex)];
            if (ImGui::BeginCombo("Environment", current.name.c_str())) {
                for (int i = 0; i < static_cast<int>(state.skyboxVariants.size()); ++i) {
                    const bool selected = i == state.selectedSkyboxIndex;
                    if (ImGui::Selectable(state.skyboxVariants[static_cast<size_t>(i)].name.c_str(), selected)) {
                        state.selectedSkyboxIndex = i;
                        const auto& variant = state.skyboxVariants[static_cast<size_t>(i)];
                        state.skyCubemap = variant.cubemap;
                        state.activeSkyboxName = variant.name;
                        state.loggedSkyboxDrawState = false;
                        LOG_INFO("SceneRenderer: switched skybox to '{}'", state.activeSkyboxName);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Text("Path: %s", current.path.c_str());
            ImGui::Text("IBL: precomputed");
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

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool needsPipelineRefresh = false;
        for (const auto& object : scene->getObjects()) {
            needsPipelineRefresh |= drawObjectDebug(object);
        }
        if (needsPipelineRefresh) {
            renderDataDirty_ = true;
            ++renderDataDirtyVersion_;
        }
    }

    if (ImGui::CollapsingHeader("Render Passes")) {
        for (const auto& pass : state.passes) {
            if (!pass.logicalPass) {
                continue;
            }
            ImGui::Text(
                "%s  objects %zu  swapchain %d",
                pass.logicalPass->getName().c_str(),
                pass.logicalPass->getObjects().size(),
                pass.usesSwapchain ? 1 : 0);
        }
    }

    ImGui::End();
}

void SceneRenderer::start() {
    if (running_) return;
    running_ = true;
    renderThread_ = std::thread(&SceneRenderer::renderLoop, this);
}

void SceneRenderer::stop() {
    if (!running_) return;
    running_ = false;
    if (renderThread_.joinable()) renderThread_.join();
}

void SceneRenderer::renderLoop() {
    while (running_) {
        std::shared_ptr<Scene> scene;
        bool dirty = false;
        uint64_t dirtyVersion = 0;
        {
            std::lock_guard<std::mutex> lock(sceneMutex_);
            scene = currentScene_;
            dirty = renderDataDirty_;
            dirtyVersion = renderDataDirtyVersion_;
        }

        if (!scene) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (dirty) {
            processScene(scene);
            std::lock_guard<std::mutex> lock(sceneMutex_);
            if (renderDataDirtyVersion_ == dirtyVersion) {
                renderDataDirty_ = false;
            }
        }

        if (window_.wasResized()) {
            window_.resetResizedFlag();
            while (window_.getWidth() == 0 || window_.getHeight() == 0) {
                window_.waitEvents();
                window_.pollEvents();
            }
            renderState_->device->handleResize(window_);
            if (auto* cam = scene->getPrimaryCamera()) {
                cam->setAspect(
                    static_cast<float>(window_.getWidth()) /
                    static_cast<float>(window_.getHeight()));
            }
            processScene(scene);
        }

        renderState_->device->checkSwapchain();
        renderFrame(*scene);
    }
}

void SceneRenderer::processScene(const std::shared_ptr<Scene>& scene) {
    auto& state = *renderState_;
    auto& device = *state.device;
    if (!scene) {
        return;
    }

    std::shared_ptr<PipelineBase> pipeline;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        pipeline = currentPipeline_;
    }

    if (!pipeline) {
        pipeline = PBRPipeline::create();
    }
    pipeline->GenPass(scene);
    for (const auto& error : pipeline->validatePassDependencies()) {
        LOG_WARN("SceneRenderer: pipeline dependency issue: {}", error);
    }

    // Rebuilding the render graph releases vertex/index buffers, descriptor
    // sets, pipelines, and render targets owned by the previous graph. Frames
    // submitted before a pipeline/scene switch may still reference them, so
    // complete all in-flight work before destroying those resources.
    device.waitIdle();

    state.renderTextures.clear();
    state.materialTextures.clear();
    state.meshes.clear();
    state.passes.clear();
    state.defaultDiffuse.reset();
    state.defaultNormal.reset();
    state.defaultEmissive.reset();
    state.defaultMsa.reset();
    state.skyboxVertexBuffer.reset();
    state.skyboxIndexBuffer.reset();
    state.skyboxIndexCount = 0;
    state.renderTextureBytes = 0;
    state.loggedSubmeshMaterialBindings = false;

    // The device is already idle. Flush the Vulkan objects whose wrappers
    // were released above before allocating the replacement render graph.
    device.waitIdle();

    for (const auto& texture : pipeline->getTextures()) {
        RenderTextureDesc desc;
        desc.name = texture.name;
        desc.width = resolveTextureWidth(texture, device);
        desc.height = resolveTextureHeight(texture, device);
        desc.format = toRHIFormat(texture.format);
        desc.external = texture.external;

        auto& frameTextures = state.renderTextures[texture.name];
        frameTextures.resize(maxFramesInFlight_);
        for (uint32_t frame = 0; frame < maxFramesInFlight_; ++frame) {
            frameTextures[frame] = device.createRenderTexture(desc);
        }
        if (!desc.external) {
            state.renderTextureBytes +=
                static_cast<uint64_t>(desc.width) *
                static_cast<uint64_t>(desc.height) *
                bytesPerPixel(desc.format) *
                maxFramesInFlight_;
        }
    }

    std::vector<std::shared_ptr<Object>> objects;
    std::unordered_set<const Object*> visited;
    for (const auto& object : scene->getObjects()) {
        collectSceneObjects(object, objects, visited);
    }

    for (const auto& object : objects) {
        const auto mesh = object->getMesh();
        if (!mesh || state.meshes.contains(mesh.get())) {
            continue;
        }

        MeshResources resources;
        const auto vertexSize = mesh->getVertices().size() * sizeof(MeshVertex);
        const auto indexSize = mesh->getIndices().size() * sizeof(uint32_t);
        resources.vertexBuffer = device.createVertexBuffer(vertexSize);
        resources.indexBuffer = device.createIndexBuffer(indexSize);
        resources.indexCount = static_cast<uint32_t>(mesh->getIndices().size());

        if (vertexSize > 0) {
            device.uploadBuffer(*resources.vertexBuffer, mesh->getVertices().data(), vertexSize);
        }
        if (indexSize > 0) {
            device.uploadBuffer(*resources.indexBuffer, mesh->getIndices().data(), indexSize);
        }

        LOG_INFO(
            "SceneRenderer: uploaded mesh '{}' vertices {} indices {}",
            object->getName(),
            mesh->getVertexCount(),
            mesh->getIndexCount());
        state.meshes.emplace(mesh.get(), std::move(resources));
    }

    state.defaultDiffuse = device.createTexture("res\\Textures\\Taffy\\cloth.png", true, FormatRGBA8Srgb);
    state.defaultNormal = device.createTexture("res\\Textures\\Taffy\\neutral_normal.png", true, FormatRGBA8Unorm);
    state.defaultEmissive = device.createTexture("res\\Textures\\Taffy\\neutral_emissive.png", true, FormatRGBA8Srgb);
    state.defaultMsa = device.createTexture("res\\Textures\\Taffy\\neutral_mra.png", true, FormatRGBA8Unorm);

    std::string preferredSkyboxPath;
    for (const auto& object : objects) {
        const auto skybox = std::dynamic_pointer_cast<Skybox>(object);
        if (!skybox || !skybox->getCubemap()) {
            continue;
        }
        preferredSkyboxPath = skybox->getCubemap()->getFilePath();
        break;
    }

    if (!preferredSkyboxPath.empty()) {
        prepareSkyboxVariants(preferredSkyboxPath);
    } else {
        state.skyCubemap.reset();
    }

    if (state.skyCubemap) {

        const auto& skyboxVertices = getSkyboxVertices();
        const auto& skyboxIndices = getSkyboxIndices();
        const auto vertexSize = skyboxVertices.size() * sizeof(SkyboxVertexData);
        const auto indexSize = skyboxIndices.size() * sizeof(uint32_t);
        state.skyboxVertexBuffer = device.createVertexBuffer(vertexSize);
        state.skyboxIndexBuffer = device.createIndexBuffer(indexSize);
        state.skyboxIndexCount = static_cast<uint32_t>(skyboxIndices.size());
        device.uploadBuffer(*state.skyboxVertexBuffer, skyboxVertices.data(), vertexSize);
        device.uploadBuffer(*state.skyboxIndexBuffer, skyboxIndices.data(), indexSize);
        LOG_INFO(
            "SceneRenderer: active skybox '{}' loaded, indices {}",
            state.activeSkyboxName,
            state.skyboxIndexCount);
    }

    for (const auto& pass : pipeline->getPasses()) {
        if (!passHasExecutableWork(*pass)) {
            continue;
        }

        std::vector<PassDesc> framePassDescs(maxFramesInFlight_);
        for (uint32_t frame = 0; frame < maxFramesInFlight_; ++frame) {
            auto& passDesc = framePassDescs[frame];
            passDesc.name = pass->getName();
            passDesc.width = device.getSwapchainWidth();
            passDesc.height = device.getSwapchainHeight();

            for (const auto& attachment : pass->getColorAttachments()) {
                const auto textureDesc = pipeline->getTexture(attachment.resource);
                const auto found = state.renderTextures.find(attachment.resource);
                if (textureDesc && textureDesc->external) {
                    passDesc.width = device.getSwapchainWidth();
                    passDesc.height = device.getSwapchainHeight();
                } else if (textureDesc) {
                    passDesc.width = resolveTextureWidth(*textureDesc, device);
                    passDesc.height = resolveTextureHeight(*textureDesc, device);
                }

                const auto image =
                    found != state.renderTextures.end() && frame < found->second.size()
                        ? found->second[frame]
                        : nullptr;
                passDesc.colorAttachments.push_back({
                    attachment.resource,
                    image,
                    toRHILoad(attachment.load),
                    toRHIStore(attachment.store),
                    false,
                    pass->getClearColor()
                });
            }

            if (const auto* depth = pass->getDepthAttachment()) {
                const auto textureDesc = pipeline->getTexture(depth->resource);
                const auto found = state.renderTextures.find(depth->resource);
                if (textureDesc) {
                    passDesc.width = resolveTextureWidth(*textureDesc, device);
                    passDesc.height = resolveTextureHeight(*textureDesc, device);
                }

                const auto image =
                    found != state.renderTextures.end() && frame < found->second.size()
                        ? found->second[frame]
                        : nullptr;
                passDesc.depthAttachment = RHIAttachmentDesc{
                    depth->resource,
                    image,
                    toRHILoad(depth->load),
                    toRHIStore(depth->store),
                    depth->readOnly,
                    TSVec4f(0.0f),
                    depth->clearDepth
                };
            }
        }

        const auto uploadMaterialTextures = [&](const std::shared_ptr<Material>& material) {
            if (!material) {
                return;
            }
            for (const auto& requirement : pass->getMaterialTextures()) {
                const auto* binding = material->resolveTexture(requirement);
                if (!binding || binding->path.empty()) {
                    continue;
                }
                const auto cacheKey = materialTextureCacheKey(binding->path, requirement.colorSpace);
                if (state.materialTextures.contains(cacheKey)) {
                    continue;
                }
                state.materialTextures[cacheKey] =
                    device.createTexture(binding->path, true, isSRGB(requirement.colorSpace)
                        ? FormatRGBA8Srgb
                        : FormatRGBA8Unorm);
            }
        };

        for (const auto& objectRef : pass->getObjects()) {
            const auto object = objectRef.lock();
            if (!object) {
                continue;
            }

            uploadMaterialTextures(object->getMaterial());
            if (const auto mesh = object->getMesh()) {
                for (const auto& submesh : mesh->getSubmeshes()) {
                    uploadMaterialTextures(submesh.getMaterial());
                }
            }
        }

        PassResources resources;
        resources.logicalPass = pass;
        resources.usesSwapchain = false;
        for (const auto& attachment : pass->getColorAttachments()) {
            if (const auto* texture = pipeline->getTexture(attachment.resource);
                texture && texture->external) {
                resources.usesSwapchain = true;
            }
        }

        resources.rhiPasses.reserve(maxFramesInFlight_);
        for (auto& passDesc : framePassDescs) {
            resources.rhiPasses.push_back(device.createPass(std::move(passDesc)));
        }

        if (passUsesSkyboxDraw(*pass)) {
            resources.descriptorSetLayout = device.createDescriptorSetLayout({
                {
                    DescriptorResourceType::UniformBuffer,
                    DescriptorResourceType::CombinedImageSampler
                },
                {
                    ShaderStageVertex,
                    ShaderStageFragment
                }
            });
            resources.descriptorPool = device.createDescriptorPool(maxFramesInFlight_, {
                {DescriptorResourceType::UniformBuffer, maxFramesInFlight_},
                {DescriptorResourceType::CombinedImageSampler, maxFramesInFlight_}
            });
            resources.descriptorSets.resize(maxFramesInFlight_);
            resources.uniformBuffers.resize(maxFramesInFlight_);
            for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
                resources.descriptorSets[i] =
                    device.allocateDescriptorSet(*resources.descriptorPool, *resources.descriptorSetLayout);
                resources.uniformBuffers[i] = device.createUniformBuffer(sizeof(SkyUniformBufferObject));
            }
        } else if (passUsesFullscreenDraw(*pass)) {
            std::vector<DescriptorResourceType> bindingTypes;
            std::vector<uint32_t> stageFlags;
            uint32_t maxBinding = 0;
            for (const auto& input : pass->getSampledTextures()) {
                maxBinding = std::max(maxBinding, input.binding);
            }
            if (pass->getName() == "Lighting") {
                maxBinding = std::max(maxBinding, 10u);
            }
            bindingTypes.resize(static_cast<size_t>(maxBinding) + 1, DescriptorResourceType::CombinedImageSampler);
            stageFlags.resize(static_cast<size_t>(maxBinding) + 1, ShaderStageFragment);
            bindingTypes[0] = DescriptorResourceType::UniformBuffer;
            stageFlags[0] = ShaderStageVertex | ShaderStageFragment;
            for (const auto& input : pass->getSampledTextures()) {
                bindingTypes[input.binding] = DescriptorResourceType::CombinedImageSampler;
                stageFlags[input.binding] = ShaderStageFragment;
            }

            std::vector<DescriptorPoolSizeDesc> poolSizes = {
                {DescriptorResourceType::UniformBuffer, maxFramesInFlight_}
            };
            if (maxBinding > 0) {
                poolSizes.push_back({
                    DescriptorResourceType::CombinedImageSampler,
                    maxBinding * maxFramesInFlight_
                });
            }

            resources.descriptorSetLayout = device.createDescriptorSetLayout({
                std::move(bindingTypes),
                std::move(stageFlags)
            });
            resources.descriptorPool = device.createDescriptorPool(maxFramesInFlight_, poolSizes);
            resources.descriptorSets.resize(maxFramesInFlight_);
            resources.uniformBuffers.resize(maxFramesInFlight_);
            for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
                resources.descriptorSets[i] =
                    device.allocateDescriptorSet(*resources.descriptorPool, *resources.descriptorSetLayout);
                resources.uniformBuffers[i] = device.createUniformBuffer(sizeof(UniformBufferObject));
            }
        } else if (
            pass->getName() == "Forward" ||
            pass->getType() == PipelinePassType::Transparent) {
            resources.descriptorSetsPerFrame = countMeshDrawSlots(*pass);
            const uint32_t descriptorSetCount = maxFramesInFlight_ * resources.descriptorSetsPerFrame;
            resources.descriptorSetLayout = device.createDescriptorSetLayout({
                {
                    DescriptorResourceType::UniformBuffer,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler,
                    DescriptorResourceType::CombinedImageSampler
                },
                {
                    ShaderStageVertex | ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment,
                    ShaderStageFragment
                }
            });
            resources.descriptorPool = device.createDescriptorPool(descriptorSetCount, {
                {DescriptorResourceType::UniformBuffer, descriptorSetCount},
                {DescriptorResourceType::CombinedImageSampler, 7 * descriptorSetCount}
            });
            resources.descriptorSets.resize(descriptorSetCount);
            resources.uniformBuffers.resize(descriptorSetCount);
            for (uint32_t i = 0; i < descriptorSetCount; ++i) {
                resources.descriptorSets[i] =
                    device.allocateDescriptorSet(*resources.descriptorPool, *resources.descriptorSetLayout);
                resources.uniformBuffers[i] = device.createUniformBuffer(sizeof(UniformBufferObject));
            }
        } else if (
            pass->getType() == PipelinePassType::Shadow ||
            pass->getType() == PipelinePassType::Geometry) {
            resources.descriptorSetsPerFrame = countMeshDrawSlots(*pass);
            const uint32_t descriptorSetCount = maxFramesInFlight_ * resources.descriptorSetsPerFrame;
            std::vector<DescriptorResourceType> bindingTypes = {DescriptorResourceType::UniformBuffer};
            std::vector<uint32_t> stageFlags = {ShaderStageVertex | ShaderStageFragment};
            for (size_t i = 0; i < pass->getMaterialTextures().size(); ++i) {
                bindingTypes.push_back(DescriptorResourceType::CombinedImageSampler);
                stageFlags.push_back(ShaderStageFragment);
            }

            std::vector<DescriptorPoolSizeDesc> poolSizes = {
                {DescriptorResourceType::UniformBuffer, descriptorSetCount}
            };
            if (!pass->getMaterialTextures().empty()) {
                poolSizes.push_back({
                    DescriptorResourceType::CombinedImageSampler,
                    static_cast<uint32_t>(pass->getMaterialTextures().size() * descriptorSetCount)
                });
            }

            resources.descriptorSetLayout = device.createDescriptorSetLayout({
                std::move(bindingTypes),
                std::move(stageFlags)
            });
            resources.descriptorPool = device.createDescriptorPool(descriptorSetCount, poolSizes);
            resources.descriptorSets.resize(descriptorSetCount);
            resources.uniformBuffers.resize(descriptorSetCount);
            for (uint32_t i = 0; i < descriptorSetCount; ++i) {
                resources.descriptorSets[i] =
                    device.allocateDescriptorSet(*resources.descriptorPool, *resources.descriptorSetLayout);
                resources.uniformBuffers[i] = device.createUniformBuffer(sizeof(UniformBufferObject));
            }
        }

        const auto vertexShader = pass->getVertexShader();
        const auto fragmentShader = pass->getFragmentShader();
        if (vertexShader && fragmentShader &&
            !vertexShader->getPath().empty() &&
            !fragmentShader->getPath().empty()) {
            PipelineDesc pipelineDesc;
            pipelineDesc.vertShaderPath = vertexShader->getPath();
            pipelineDesc.fragShaderPath = fragmentShader->getPath();
            if (!vertexShader->getEntry().empty()) {
                pipelineDesc.vertEntryPoint = vertexShader->getEntry();
            }
            if (!fragmentShader->getEntry().empty()) {
                pipelineDesc.fragEntryPoint = fragmentShader->getEntry();
            }
            if (passUsesSkyboxDraw(*pass)) {
                pipelineDesc.vertexStride = sizeof(SkyboxVertexData);
                pipelineDesc.attributeLocations = {0};
                pipelineDesc.attributeFormats = {FormatRGB32Float};
                pipelineDesc.attributeOffsets = {offsetof(SkyboxVertexData, pos)};
            } else if (passUsesFullscreenDraw(*pass)) {
                pipelineDesc.vertexStride = 0;
            } else {
                pipelineDesc.vertexStride = sizeof(MeshVertex);
                pipelineDesc.attributeLocations = {
                    MeshVertexLocation::Position,
                    MeshVertexLocation::Normal,
                    MeshVertexLocation::Tangent,
                    MeshVertexLocation::Bitangent,
                    MeshVertexLocation::UV0
                };
                pipelineDesc.attributeFormats = {
                    FormatRGB32Float,
                    FormatRGB32Float,
                    FormatRGB32Float,
                    FormatRGB32Float,
                    FormatRG32Float
                };
                pipelineDesc.attributeOffsets = {
                    offsetof(MeshVertex, position),
                    offsetof(MeshVertex, normal),
                    offsetof(MeshVertex, tangent),
                    offsetof(MeshVertex, bitangent),
                    offsetof(MeshVertex, uv0)
                };
            }
            pipelineDesc.cullMode = toRHICullMode(pass->getCullMode());
            pipelineDesc.depthTest = pass->getDepthTest();
            pipelineDesc.depthWrite = pass->getDepthWrite();
            pipelineDesc.depthCompareOp = toRHICompare(pass->getDepthTestMode());
            pipelineDesc.descriptorSetLayout = resources.descriptorSetLayout;
            pipelineDesc.useMSAA = resources.usesSwapchain;

            for (const auto& attachment : pass->getColorAttachments()) {
                if (const auto* texture = pipeline->getTexture(attachment.resource)) {
                    pipelineDesc.colorAttachmentFormats.push_back(
                        device.resolveRenderTextureFormat(toRHIFormat(texture->format)));
                }
            }
            if (resources.usesSwapchain) {
                pipelineDesc.depthAttachmentFormat = device.getDepthFormat();
            }
            if (const auto* depth = pass->getDepthAttachment()) {
                if (const auto* texture = pipeline->getTexture(depth->resource)) {
                    pipelineDesc.depthAttachmentFormat =
                        device.resolveRenderTextureFormat(toRHIFormat(texture->format));
                }
            }

            if (!passHasExecutableWork(*pass)) {
                continue;
            }

            LOG_INFO(
                "SceneRenderer: creating pipeline '{}' colors {} depthFormat {} swapchain {}",
                pass->getName(),
                pipelineDesc.colorAttachmentFormats.size(),
                pipelineDesc.depthAttachmentFormat,
                resources.usesSwapchain ? 1 : 0);
            resources.gpuPipeline = device.createGraphicsPipeline(pipelineDesc);
        }

        state.passes.push_back(std::move(resources));
    }

    LOG_INFO(
        "SceneRenderer: parsed '{}' into {} render textures, {} meshes, {} passes",
        pipeline->getName(),
        state.renderTextures.size(),
        state.meshes.size(),
        state.passes.size());
}

void SceneRenderer::renderFrame(Scene& scene) {
    auto& state = *renderState_;
    auto& device = *state.device;
    auto& cmdList = *state.commandList;

    if (!scene.getPrimaryCamera() || state.passes.empty()) {
        return;
    }

    if (!device.beginFrame(cmdList)) {
        return;
    }

    const uint32_t frameIdx = device.getCurrentFrameIndex();
    bool drawUI = state.ui && device.beginUIFrame(*state.ui);

    auto* cam = scene.getPrimaryCamera();
    TSMat4f viewMat = cam->getViewMatrix();
    const TSMat4f unflippedProjMat = cam->getProjectionMatrix();
    TSMat4f projMat = unflippedProjMat;
    projMat[1][1] *= -1;

    TSVec3f lightDir(-0.5f, -1.0f, -0.8f);
    TSVec3f lightColor(1.0f);
    float lightIntensity = 10.0f;
    if (!scene.getLights().empty()) {
        if (auto* light = dynamic_cast<DirectionalLight*>(scene.getLights()[0].get())) {
            lightDir = light->getDirection();
            lightColor = light->getColor();
            lightIntensity = light->getIntensity();
        }
    }

    for (auto& pass : state.passes) {
        if (!pass.descriptorSetLayout ||
            frameIdx >= pass.descriptorSets.size() ||
            frameIdx >= pass.uniformBuffers.size()) {
            continue;
        }

        if (passUsesSkyboxDraw(*pass.logicalPass)) {
            TSMat4f skyView = TSMat4f(TSMat3f(viewMat));
            SkyUniformBufferObject skyUbo{};
            skyUbo.view = transpose(skyView);
            skyUbo.proj = transpose(projMat);
            pass.uniformBuffers[frameIdx]->setData(&skyUbo, sizeof(skyUbo));

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[frameIdx]},
                {1, DescriptorResourceType::CombinedImageSampler, nullptr, state.skyCubemap}
            });
        } else if (passUsesFullscreenDraw(*pass.logicalPass)) {
            UniformBufferObject ubo{};
            ubo.model = transpose(TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), 1.0f);
            const bool debugPostProcess =
                pass.logicalPass->getType() == PipelinePassType::PostProcess &&
                !state.debugOutputResource.empty();
            ubo.roughnessAo = TSVec4f(
                1.0f,
                1.0f,
                debugPostProcess ? 1.0f : static_cast<float>(state.materialDebugMode),
                0.0f);
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            pass.uniformBuffers[frameIdx]->setData(&ubo, sizeof(ubo));

            std::vector<DescriptorWriteDesc> writes;
            writes.push_back({
                0,
                DescriptorResourceType::UniformBuffer,
                pass.uniformBuffers[frameIdx]
            });

            for (const auto& input : pass.logicalPass->getSampledTextures()) {
                const auto resourceName =
                    debugPostProcess && input.binding == 1
                        ? state.debugOutputResource
                        : input.resource;
                const auto found = state.renderTextures.find(resourceName);
                if (found == state.renderTextures.end() ||
                    frameIdx >= found->second.size() ||
                    !found->second[frameIdx]) {
                    LOG_WARN(
                        "SceneRenderer: pass '{}' missing sampled texture '{}' for slot '{}'",
                        pass.logicalPass->getName(),
                        resourceName,
                        input.slot);
                    continue;
                }
                writes.push_back({
                    input.binding,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    found->second[frameIdx]
                });
            }

            if (pass.logicalPass->getName() == "Lighting") {
                const auto& iblName = state.activeSkyboxName;
                auto irradianceInfo = device.getIBLDescriptorInfo(IBLMapType::Irradiance, iblName);
                auto prefilteredInfo = device.getIBLDescriptorInfo(IBLMapType::Prefiltered, iblName);
                auto brdfInfo = device.getIBLDescriptorInfo(IBLMapType::BrdfLut, iblName);
                if (irradianceInfo.nativeView == 0 ||
                    prefilteredInfo.nativeView == 0 ||
                    brdfInfo.nativeView == 0) {
                    LOG_WARN("SceneRenderer: missing precomputed IBL descriptors for skybox '{}'", iblName);
                }
                writes.push_back({
                    8,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    nullptr,
                    irradianceInfo
                });
                writes.push_back({
                    9,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    nullptr,
                    prefilteredInfo
                });
                writes.push_back({
                    10,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    nullptr,
                    brdfInfo
                });
            }

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], writes);
        } else if (pass.logicalPass->getExecution() == PipelinePassExecution::Mesh) {
            // Mesh pass descriptors contain per-draw model/material state and are updated when each submesh is drawn.
        } else if (
            pass.logicalPass->getName() == "Forward" ||
            pass.logicalPass->getType() == PipelinePassType::Transparent) {
            std::shared_ptr<Object> drawObject;
            std::shared_ptr<Material> material;
            for (const auto& objectRef : pass.logicalPass->getObjects()) {
                const auto object = objectRef.lock();
                if (!drawObject && object && object->getMesh()) {
                    drawObject = object;
                }
                material = object ? object->getMaterial() : nullptr;
                if (material) {
                    break;
                }
            }

            const auto resolveImage =
                [&](MaterialTextureSemantic semantic,
                    const std::shared_ptr<Image>& fallback) -> std::shared_ptr<Image> {
                    if (!material) {
                        return fallback;
                    }
                    const auto path = material->getTexture(semantic);
                    if (path.empty()) {
                        return fallback;
                    }
                    const auto found = state.materialTextures.find(
                        materialTextureCacheKey(path, colorSpaceForSemantic(semantic)));
                    return found == state.materialTextures.end() ? fallback : found->second;
                };

            const float metallic = state.pbrMetallic;
            const float roughness = state.pbrRoughness;
            const float ao = state.pbrAo;

            const auto& iblName = state.activeSkyboxName;
            auto irradianceInfo = device.getIBLDescriptorInfo(IBLMapType::Irradiance, iblName);
            auto prefilteredInfo = device.getIBLDescriptorInfo(IBLMapType::Prefiltered, iblName);
            auto brdfInfo = device.getIBLDescriptorInfo(IBLMapType::BrdfLut, iblName);
            if (irradianceInfo.nativeView == 0 ||
                prefilteredInfo.nativeView == 0 ||
                brdfInfo.nativeView == 0) {
                LOG_WARN("SceneRenderer: missing precomputed IBL descriptors for skybox '{}'", iblName);
            }

            UniformBufferObject ubo{};
            ubo.model = transpose(drawObject ? drawObject->getModelMatrix() : TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), metallic);
            ubo.roughnessAo = TSVec4f(
                roughness,
                ao,
                static_cast<float>(state.materialDebugMode),
                static_cast<float>(state.bodyUvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            pass.uniformBuffers[frameIdx]->setData(&ubo, sizeof(ubo));

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[frameIdx]},
                {1, DescriptorResourceType::CombinedImageSampler, nullptr,
                    resolveImage(MaterialTextureSemantic::BaseColor, state.defaultDiffuse)},
                {2, DescriptorResourceType::CombinedImageSampler, nullptr,
                    resolveImage(MaterialTextureSemantic::Normal, state.defaultNormal)},
                {3, DescriptorResourceType::CombinedImageSampler, nullptr,
                    resolveImage(MaterialTextureSemantic::Emissive, state.defaultEmissive)},
                {4, DescriptorResourceType::CombinedImageSampler, nullptr,
                    resolveImage(MaterialTextureSemantic::MetallicRoughnessAO, state.defaultMsa)},
                {5, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, irradianceInfo},
                {6, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, prefilteredInfo},
                {7, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, brdfInfo}
            });
        } else if (
            pass.logicalPass->getType() == PipelinePassType::Shadow ||
            pass.logicalPass->getType() == PipelinePassType::Geometry) {
            std::shared_ptr<Object> drawObject;
            std::shared_ptr<Material> material;
            for (const auto& objectRef : pass.logicalPass->getObjects()) {
                const auto object = objectRef.lock();
                if (!drawObject && object && object->getMesh()) {
                    drawObject = object;
                }
                material = object ? object->getMaterial() : nullptr;
                if (material) {
                    break;
                }
            }

            const auto resolveImage =
                [&](MaterialTextureSemantic semantic,
                    const std::shared_ptr<Image>& fallback) -> std::shared_ptr<Image> {
                    if (!material) {
                        return fallback;
                    }
                    const auto path = material->getTexture(semantic);
                    if (path.empty()) {
                        return fallback;
                    }
                    const auto found = state.materialTextures.find(
                        materialTextureCacheKey(path, colorSpaceForSemantic(semantic)));
                    return found == state.materialTextures.end() ? fallback : found->second;
                };

            const float metallic = state.pbrMetallic;
            const float roughness = state.pbrRoughness;
            const float ao = state.pbrAo;

            UniformBufferObject ubo{};
            ubo.model = transpose(drawObject ? drawObject->getModelMatrix() : TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), metallic);
            ubo.roughnessAo = TSVec4f(
                roughness,
                ao,
                static_cast<float>(state.materialDebugMode),
                static_cast<float>(state.bodyUvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            pass.uniformBuffers[frameIdx]->setData(&ubo, sizeof(ubo));

            std::vector<DescriptorWriteDesc> writes = {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[frameIdx]}
            };
            if (pass.logicalPass->getType() == PipelinePassType::Geometry &&
                !pass.logicalPass->getMaterialTextures().empty()) {
                writes.push_back({
                    1,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveImage(MaterialTextureSemantic::BaseColor, state.defaultDiffuse)
                });
            }

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], writes);
        }
    }

    const auto resolveMaterialImage =
        [&](const std::shared_ptr<Material>& material,
            MaterialTextureSemantic semantic,
            const std::shared_ptr<Image>& fallback) -> std::shared_ptr<Image> {
            if (!material) {
                return fallback;
            }
            const auto path = material->getTexture(semantic);
            if (path.empty()) {
                return fallback;
            }
            const auto found = state.materialTextures.find(
                materialTextureCacheKey(path, colorSpaceForSemantic(semantic)));
            return found == state.materialTextures.end() ? fallback : found->second;
        };

    const auto updateMeshDrawDescriptors =
        [&](PassResources& pass,
            const Object& object,
            const std::shared_ptr<Material>& submeshMaterial,
            uint32_t submeshIndex,
            const std::string& submeshMaterialName,
            uint32_t descriptorSlot) -> const DescriptorSet* {
            const uint32_t descriptorIndex = frameIdx * pass.descriptorSetsPerFrame + descriptorSlot;
            if (descriptorIndex >= pass.descriptorSets.size() ||
                descriptorIndex >= pass.uniformBuffers.size()) {
                LOG_WARN(
                    "SceneRenderer: pass '{}' descriptor slot {} out of {}",
                    pass.logicalPass ? pass.logicalPass->getName() : std::string("<null>"),
                    descriptorSlot,
                    pass.descriptorSetsPerFrame);
                return nullptr;
            }

            const float metallic = state.pbrMetallic;
            const float roughness = state.pbrRoughness;
            const float ao = state.pbrAo;

            UniformBufferObject ubo{};
            ubo.model = transpose(object.getModelMatrix());
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(object.getFlipProjectionY() ? projMat : unflippedProjMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), metallic);
            const int uvMode = selectMaterialUvMode(
                submeshMaterialName,
                state.bodyUvMode,
                state.hairUvMode,
                state.faceUvMode);
            ubo.roughnessAo = TSVec4f(
                roughness,
                ao,
                static_cast<float>(state.materialDebugMode),
                static_cast<float>(uvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            pass.uniformBuffers[descriptorIndex]->setData(&ubo, sizeof(ubo));

            std::vector<DescriptorWriteDesc> writes = {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[descriptorIndex]}
            };

            const auto material = submeshMaterial ? submeshMaterial : object.getMaterial();
            if (!state.loggedSubmeshMaterialBindings) {
                LOG_INFO(
                    "SceneRenderer: pass '{}' submesh {} '{}' baseColor '{}'",
                    pass.logicalPass ? pass.logicalPass->getName() : std::string("<null>"),
                    submeshIndex,
                    submeshMaterialName,
                    material ? material->getTexture(MaterialTextureSemantic::BaseColor) : std::string("<null>"));
            }
            if (pass.logicalPass->getName() == "Forward" ||
                pass.logicalPass->getType() == PipelinePassType::Transparent) {
                const auto& iblName = state.activeSkyboxName;
                auto irradianceInfo = device.getIBLDescriptorInfo(IBLMapType::Irradiance, iblName);
                auto prefilteredInfo = device.getIBLDescriptorInfo(IBLMapType::Prefiltered, iblName);
                auto brdfInfo = device.getIBLDescriptorInfo(IBLMapType::BrdfLut, iblName);
                if (irradianceInfo.nativeView == 0 ||
                    prefilteredInfo.nativeView == 0 ||
                    brdfInfo.nativeView == 0) {
                    LOG_WARN("SceneRenderer: missing precomputed IBL descriptors for skybox '{}'", iblName);
                }
                writes.push_back({
                    1,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveMaterialImage(material, MaterialTextureSemantic::BaseColor, state.defaultDiffuse)
                });
                writes.push_back({
                    2,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveMaterialImage(material, MaterialTextureSemantic::Normal, state.defaultNormal)
                });
                writes.push_back({
                    3,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveMaterialImage(material, MaterialTextureSemantic::Emissive, state.defaultEmissive)
                });
                writes.push_back({
                    4,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveMaterialImage(material, MaterialTextureSemantic::MetallicRoughnessAO, state.defaultMsa)
                });
                writes.push_back({5, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, irradianceInfo});
                writes.push_back({6, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, prefilteredInfo});
                writes.push_back({7, DescriptorResourceType::CombinedImageSampler, nullptr, nullptr, brdfInfo});
            } else if (
                pass.logicalPass->getType() == PipelinePassType::Geometry &&
                !pass.logicalPass->getMaterialTextures().empty()) {
                writes.push_back({
                    1,
                    DescriptorResourceType::CombinedImageSampler,
                    nullptr,
                    resolveMaterialImage(material, MaterialTextureSemantic::BaseColor, state.defaultDiffuse)
                });
            }

            device.updateDescriptorSet(pass.descriptorSets[descriptorIndex], writes);
            return &pass.descriptorSets[descriptorIndex];
        };

    bool swapchainPassOpen = false;
    for (auto& pass : state.passes) {
        if (frameIdx >= pass.rhiPasses.size() || !pass.rhiPasses[frameIdx]) {
            continue;
        }
        const auto& activeRhiPass = pass.rhiPasses[frameIdx];
        if (!pass.usesSwapchain && !pass.gpuPipeline) {
            continue;
        }

        const auto& passDesc = activeRhiPass->getDesc();
        cmdList.setViewport(
            0.0f,
            0.0f,
            static_cast<float>(passDesc.width),
            static_cast<float>(passDesc.height));
        cmdList.setScissor(0, 0, passDesc.width, passDesc.height);

        if (pass.usesSwapchain) {
            if (!swapchainPassOpen) {
                device.beginFrameRenderPass(cmdList);
                swapchainPassOpen = true;
            }
        } else {
            if (swapchainPassOpen) {
                device.endFrameRenderPass(cmdList);
                if (drawUI) {
                    device.renderUI(*state.ui, cmdList);
                    drawUI = false;
                }
                swapchainPassOpen = false;
            }
            for (const auto& attachment : activeRhiPass->getDesc().colorAttachments) {
                if (attachment.image) {
                    cmdList.transitionImage(
                        *attachment.image,
                        attachment.load == RHIAttachmentLoad::Load
                            ? ImageLayout::ShaderRead
                            : ImageLayout::Undefined,
                        ImageLayout::ColorAttachment);
                }
            }
            if (const auto& depth = activeRhiPass->getDesc().depthAttachment;
                depth && depth->image) {
                const auto newLayout = depth->readOnly
                    ? ImageLayout::DepthReadOnly
                    : ImageLayout::DepthAttachment;
                const auto oldLayout = depth->load == RHIAttachmentLoad::Load || depth->readOnly
                    ? ImageLayout::ShaderRead
                    : ImageLayout::Undefined;
                cmdList.transitionImage(*depth->image, oldLayout, newLayout);
            }
            cmdList.beginRenderPass(*activeRhiPass);
        }

        if (pass.gpuPipeline) {
            cmdList.bindPipeline(
                pass.gpuPipeline->getNativePipeline(),
                pass.gpuPipeline->getNativeLayout());

            if (pass.logicalPass->getExecution() != PipelinePassExecution::Mesh &&
                frameIdx < pass.descriptorSets.size()) {
                cmdList.bindDescriptorSet(0, pass.descriptorSets[frameIdx]);
            }

            if (passUsesSkyboxDraw(*pass.logicalPass)) {
                if (!state.loggedSkyboxDrawState) {
                    LOG_INFO(
                        "SceneRenderer: skybox draw state cubemap {} vb {} ib {} indices {}",
                        state.skyCubemap ? 1 : 0,
                        state.skyboxVertexBuffer ? 1 : 0,
                        state.skyboxIndexBuffer ? 1 : 0,
                        state.skyboxIndexCount);
                    state.loggedSkyboxDrawState = true;
                }
                if (state.skyboxVertexBuffer && state.skyboxIndexBuffer && state.skyboxIndexCount > 0) {
                    cmdList.bindVertexBuffer(*state.skyboxVertexBuffer);
                    cmdList.bindIndexBuffer(*state.skyboxIndexBuffer);
                    cmdList.drawIndexed(state.skyboxIndexCount);
                }
            } else if (passUsesFullscreenDraw(*pass.logicalPass)) {
                cmdList.draw(3);
            } else {
                uint32_t descriptorSlot = 0;
                for (const auto& objectRef : pass.logicalPass->getObjects()) {
                    const auto object = objectRef.lock();
                    const auto mesh = object ? object->getMesh() : nullptr;
                    if (!mesh) {
                        continue;
                    }

                    const auto resources = state.meshes.find(mesh.get());
                    if (resources == state.meshes.end() || resources->second.indexCount == 0) {
                        continue;
                    }

                    cmdList.bindVertexBuffer(*resources->second.vertexBuffer);
                    cmdList.bindIndexBuffer(*resources->second.indexBuffer);
                    cmdList.setFrontFace(
                        object->getFlipProjectionY()
                            ? FrontFaceClockwise
                            : FrontFaceCounterClockwise);
                    const auto& submeshes = mesh->getSubmeshes();
                    if (submeshes.empty()) {
                        const auto* descriptorSet =
                            updateMeshDrawDescriptors(
                                pass,
                                *object,
                                object->getMaterial(),
                                0,
                                "<mesh>",
                                descriptorSlot++);
                        if (!descriptorSet) {
                            continue;
                        }
                        cmdList.bindDescriptorSet(0, *descriptorSet);
                        cmdList.drawIndexed(resources->second.indexCount);
                    } else {
                        for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex) {
                            const auto& submesh = submeshes[submeshIndex];
                            if (submesh.getIndexCount() == 0) {
                                continue;
                            }
                            const auto* descriptorSet =
                                updateMeshDrawDescriptors(
                                    pass,
                                    *object,
                                    submesh.getMaterial(),
                                    static_cast<uint32_t>(submeshIndex),
                                    submesh.getMaterialName(),
                                    descriptorSlot++);
                            if (!descriptorSet) {
                                continue;
                            }
                            cmdList.bindDescriptorSet(0, *descriptorSet);
                            cmdList.drawIndexed(submesh.getIndexCount(), 1, submesh.getIndexOffset());
                        }
                    }
                }
            }
        }

        if (!pass.usesSwapchain) {
            cmdList.endRenderPass();
            for (const auto& attachment : activeRhiPass->getDesc().colorAttachments) {
                if (attachment.image) {
                    cmdList.transitionImage(
                        *attachment.image,
                        ImageLayout::ColorAttachment,
                        ImageLayout::ShaderRead);
                }
            }
            if (const auto& depth = activeRhiPass->getDesc().depthAttachment;
                depth && depth->image) {
                cmdList.transitionImage(
                    *depth->image,
                    depth->readOnly ? ImageLayout::DepthReadOnly : ImageLayout::DepthAttachment,
                    ImageLayout::ShaderRead);
            }
        }
    }

    if (swapchainPassOpen) {
        device.endFrameRenderPass(cmdList);
        if (drawUI) {
            device.renderUI(*state.ui, cmdList);
        }
    }

    state.loggedSubmeshMaterialBindings = true;
    device.endFrame();
}

} // namespace Tasrovy::RHI
