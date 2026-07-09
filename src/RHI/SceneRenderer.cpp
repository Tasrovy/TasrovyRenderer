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
#include <chrono>
#include <cstddef>
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
};

struct SkyUniformBufferObject {
    TSMat4f view;
    TSMat4f proj;
};

struct PassResources {
    std::shared_ptr<PipelinePass> logicalPass;
    std::shared_ptr<Pass> rhiPass;
    std::shared_ptr<Pipeline> gpuPipeline;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<DescriptorPool> descriptorPool;
    std::vector<DescriptorSet> descriptorSets;
    std::vector<std::shared_ptr<Buffer>> uniformBuffers;
    bool usesSwapchain = false;
};

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
    const std::filesystem::path resPath("res");

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

    if (ImGui::TreeNode("PBR Params")) {
        float metallic = material.getFloat("uMetallic", 1.0f);
        float roughness = material.getFloat("uRoughness", 1.0f);
        float ao = material.getFloat("uAo", 1.0f);
        if (ImGui::SliderFloat("uMetallic", &metallic, 0.0f, 1.0f)) {
            material.setFloat("uMetallic", metallic);
        }
        if (ImGui::SliderFloat("uRoughness", &roughness, 0.0f, 1.0f)) {
            material.setFloat("uRoughness", roughness);
        }
        if (ImGui::SliderFloat("uAo", &ao, 0.0f, 1.0f)) {
            material.setFloat("uAo", ao);
        }

        for (const auto& [name, value] : material.getFloatParams()) {
            if (name == "uMetallic" || name == "uRoughness" || name == "uAo") {
                continue;
            }
            ImGui::Text("%s: %.3f", name.c_str(), value);
        }
        ImGui::TreePop();
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

    TSVec3f position = object->getPosition();
    TSVec3f rotation = object->getRotationEuler();
    TSVec3f scale = object->getScale();
    if (drawVec3Control("Position", position)) {
        object->setPosition(position);
    }
    if (drawVec3Control("Rotation", rotation, 0.01f)) {
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
                    submesh.materialName.c_str(),
                    submesh.indexOffset,
                    submesh.indexCount);
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
    std::unordered_map<std::string, std::shared_ptr<Image>> renderTextures;
    std::unordered_map<std::string, std::shared_ptr<Image>> materialTextures;
    std::unordered_map<const Mesh*, MeshResources> meshes;
    std::vector<PassResources> passes;
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
    std::vector<std::shared_ptr<void>> retiredResources;
    std::unique_ptr<Tasrovy::UI::UIOverlay> ui;
    uint32_t maxFramesInFlight = 0;

    RenderState(Tasrovy::Windowing::Window& window, uint32_t maxFrames)
        : maxFramesInFlight(maxFrames) {
        device = Device::createForWindow(window, maxFrames);
        commandList = CommandList::create();
        ui = device->createUIOverlay(window);
    }
};

namespace {

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<Buffer>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<Image>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<Pipeline>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<Pass>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<DescriptorSetLayout>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

void retireResource(std::vector<std::shared_ptr<void>>& retired, const std::shared_ptr<DescriptorPool>& resource) {
    if (resource) {
        retired.push_back(resource);
    }
}

} // namespace

void SceneRenderer::retireCurrentResources() {
    auto& state = *renderState_;
    auto& retired = state.retiredResources;

    for (const auto& [_, image] : state.renderTextures) {
        retireResource(retired, image);
    }
    for (const auto& [_, image] : state.materialTextures) {
        retireResource(retired, image);
    }
    for (const auto& [_, mesh] : state.meshes) {
        retireResource(retired, mesh.vertexBuffer);
        retireResource(retired, mesh.indexBuffer);
    }
    for (const auto& pass : state.passes) {
        retireResource(retired, pass.rhiPass);
        retireResource(retired, pass.gpuPipeline);
        retireResource(retired, pass.descriptorSetLayout);
        retireResource(retired, pass.descriptorPool);
        for (const auto& buffer : pass.uniformBuffers) {
            retireResource(retired, buffer);
        }
    }

    retireResource(retired, state.defaultDiffuse);
    retireResource(retired, state.defaultNormal);
    retireResource(retired, state.defaultEmissive);
    retireResource(retired, state.defaultMsa);
    retireResource(retired, state.skyboxVertexBuffer);
    retireResource(retired, state.skyboxIndexBuffer);
}

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
    ImGui::Text("Tasrovy RHI frame");
    ImGui::Text("Passes: %zu", state.passes.size());
    ImGui::Text("Meshes: %zu", state.meshes.size());
    ImGui::Text("Render Textures: %zu", state.renderTextures.size());
    ImGui::Text("Material Textures: %zu", state.materialTextures.size());
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
            if (drawVec3Control("Rotation##Camera", rotation, 0.01f)) {
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

    device.waitIdle();
    state.retiredResources.clear();
    retireCurrentResources();

    state.renderTextures.clear();
    state.materialTextures.clear();
    state.meshes.clear();
    state.passes.clear();

    for (const auto& error : pipeline->validateResourceFlow()) {
        LOG_WARN("SceneRenderer: pipeline resource issue: {}", error);
    }

    for (const auto& texture : pipeline->getTextures()) {
        RenderTextureDesc desc;
        desc.name = texture.name;
        desc.width = resolveTextureWidth(texture, device);
        desc.height = resolveTextureHeight(texture, device);
        desc.format = toRHIFormat(texture.format);
        desc.external = texture.external;

        state.renderTextures[texture.name] = device.createRenderTexture(desc);
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

    state.defaultDiffuse = device.createTexture("res\\diffuse.png", true, FormatRGBA8Srgb);
    state.defaultNormal = device.createTexture("res\\normal.png", false, FormatRGBA8Unorm);
    state.defaultEmissive = device.createTexture("res\\emissive.png", false, FormatRGBA8Srgb);
    state.defaultMsa = device.createTexture("res\\msa.png", false, FormatRGBA8Unorm);

    state.skyboxVertexBuffer.reset();
    state.skyboxIndexBuffer.reset();
    state.skyboxIndexCount = 0;

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
        PassDesc passDesc;
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

            passDesc.colorAttachments.push_back({
                attachment.resource,
                found == state.renderTextures.end() ? nullptr : found->second,
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

            passDesc.depthAttachment = RHIAttachmentDesc{
                depth->resource,
                found == state.renderTextures.end() ? nullptr : found->second,
                toRHILoad(depth->load),
                toRHIStore(depth->store),
                depth->readOnly,
                TSVec4f(0.0f),
                depth->clearDepth
            };
        }

        for (const auto& objectRef : pass->getObjects()) {
            const auto object = objectRef.lock();
            const auto material = object ? object->getMaterial() : nullptr;
            if (!material) {
                continue;
            }

            for (const auto& requirement : pass->getMaterialTextures()) {
                const auto* binding = material->resolveTexture(requirement);
                if (!binding || binding->path.empty() || state.materialTextures.contains(binding->path)) {
                    continue;
                }
                state.materialTextures[binding->path] =
                    device.createTexture(binding->path, true, isSRGB(requirement.colorSpace)
                        ? FormatRGBA8Srgb
                        : FormatRGBA8Unorm);
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

        resources.rhiPass = device.createPass(std::move(passDesc));

        if (pass->getType() == PipelinePassType::Skybox) {
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
        } else if (
            pass->getName() == "Forward" ||
            pass->getType() == PipelinePassType::Transparent) {
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
            resources.descriptorPool = device.createDescriptorPool(maxFramesInFlight_, {
                {DescriptorResourceType::UniformBuffer, maxFramesInFlight_},
                {DescriptorResourceType::CombinedImageSampler, 7 * maxFramesInFlight_}
            });
            resources.descriptorSets.resize(maxFramesInFlight_);
            resources.uniformBuffers.resize(maxFramesInFlight_);
            for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
                resources.descriptorSets[i] =
                    device.allocateDescriptorSet(*resources.descriptorPool, *resources.descriptorSetLayout);
                resources.uniformBuffers[i] = device.createUniformBuffer(sizeof(UniformBufferObject));
            }
        } else if (
            pass->getType() == PipelinePassType::Shadow ||
            pass->getType() == PipelinePassType::Geometry) {
            resources.descriptorSetLayout = device.createDescriptorSetLayout({
                {DescriptorResourceType::UniformBuffer},
                {ShaderStageVertex}
            });
            resources.descriptorPool = device.createDescriptorPool(maxFramesInFlight_, {
                {DescriptorResourceType::UniformBuffer, maxFramesInFlight_}
            });
            resources.descriptorSets.resize(maxFramesInFlight_);
            resources.uniformBuffers.resize(maxFramesInFlight_);
            for (uint32_t i = 0; i < maxFramesInFlight_; ++i) {
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
            if (pass->getType() == PipelinePassType::Skybox) {
                pipelineDesc.vertexStride = sizeof(SkyboxVertexData);
                pipelineDesc.attributeLocations = {0};
                pipelineDesc.attributeFormats = {FormatRGB32Float};
                pipelineDesc.attributeOffsets = {offsetof(SkyboxVertexData, pos)};
            } else {
                pipelineDesc.vertexStride = sizeof(MeshVertex);
                pipelineDesc.attributeLocations = {0, 1, 2, 3, 4};
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
                    offsetof(MeshVertex, vertexColor),
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

            const bool passDrawsGeometry =
                pass->getType() == PipelinePassType::Skybox ||
                !pass->getObjects().empty();
            if (!passDrawsGeometry) {
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
    TSMat4f projMat = cam->getProjectionMatrix();
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

        if (pass.logicalPass->getType() == PipelinePassType::Skybox) {
            TSMat4f skyView = TSMat4f(TSMat3f(viewMat));
            SkyUniformBufferObject skyUbo{};
            skyUbo.view = transpose(skyView);
            skyUbo.proj = transpose(projMat);
            pass.uniformBuffers[frameIdx]->setData(&skyUbo, sizeof(skyUbo));

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[frameIdx]},
                {1, DescriptorResourceType::CombinedImageSampler, nullptr, state.skyCubemap}
            });
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
                    const auto found = state.materialTextures.find(path);
                    return found == state.materialTextures.end() ? fallback : found->second;
                };

            const float metallic = material ? material->getFloat("uMetallic", 1.0f) : 1.0f;
            const float roughness = material ? material->getFloat("uRoughness", 1.0f) : 1.0f;
            const float ao = material ? material->getFloat("uAo", 1.0f) : 1.0f;

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
            ubo.roughnessAo = TSVec4f(roughness, ao, 0.0f, 0.0f);
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
            UniformBufferObject ubo{};
            TSMat4f modelMat(1.0f);
            for (const auto& objectRef : pass.logicalPass->getObjects()) {
                const auto object = objectRef.lock();
                if (object && object->getMesh()) {
                    modelMat = object->getModelMatrix();
                    break;
                }
            }
            ubo.model = transpose(modelMat);
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            pass.uniformBuffers[frameIdx]->setData(&ubo, sizeof(ubo));

            device.updateDescriptorSet(pass.descriptorSets[frameIdx], {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[frameIdx]}
            });
        }
    }

    bool swapchainPassOpen = false;
    for (const auto& pass : state.passes) {
        if (!pass.rhiPass) {
            continue;
        }
        if (!pass.usesSwapchain && !pass.gpuPipeline) {
            continue;
        }

        const auto& passDesc = pass.rhiPass->getDesc();
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
            for (const auto& attachment : pass.rhiPass->getDesc().colorAttachments) {
                if (attachment.image) {
                    cmdList.transitionImage(
                        *attachment.image,
                        ImageLayout::Undefined,
                        ImageLayout::ColorAttachment);
                }
            }
            if (const auto& depth = pass.rhiPass->getDesc().depthAttachment;
                depth && depth->image && !depth->readOnly) {
                cmdList.transitionImage(
                    *depth->image,
                    ImageLayout::Undefined,
                    ImageLayout::DepthAttachment);
            }
            cmdList.beginRenderPass(*pass.rhiPass);
        }

        if (pass.gpuPipeline) {
            cmdList.bindPipeline(
                pass.gpuPipeline->getNativePipeline(),
                pass.gpuPipeline->getNativeLayout());

            if (frameIdx < pass.descriptorSets.size()) {
                cmdList.bindDescriptorSet(0, pass.descriptorSets[frameIdx]);
            }

            if (pass.logicalPass->getType() == PipelinePassType::Skybox) {
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
            } else {
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
                    cmdList.drawIndexed(resources->second.indexCount);
                }
            }
        }

        if (!pass.usesSwapchain) {
            cmdList.endRenderPass();
            for (const auto& attachment : pass.rhiPass->getDesc().colorAttachments) {
                if (attachment.image) {
                    cmdList.transitionImage(
                        *attachment.image,
                        ImageLayout::ColorAttachment,
                        ImageLayout::ShaderRead);
                }
            }
            if (const auto& depth = pass.rhiPass->getDesc().depthAttachment;
                depth && depth->image && !depth->readOnly) {
                cmdList.transitionImage(
                    *depth->image,
                    ImageLayout::DepthAttachment,
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

    device.endFrame();
}

} // namespace Tasrovy::RHI
