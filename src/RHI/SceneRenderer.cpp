#include "SceneRenderer.h"

#include "Buffer.h"
#include "CommandList.h"
#include "Device.h"
#include "Descriptor.h"
#include "Image.h"
#include "Pass.h"
#include "Pipeline.h"
#include "RHIConfig.h"
#include "ResourceMonitor.h"
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
#include <array>
#include <chrono>
#include <cmath>
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

constexpr size_t MaxSceneLights = 8;

struct GpuLightData {
    // positionAndType.w: 0 directional, 1 point, 2 area.
    TSVec4f positionAndType = TSVec4f(0.0f);
    // directionAndRange.xyz: emission direction; w: optional range.
    TSVec4f directionAndRange = TSVec4f(0.0f);
    // colorAndIntensity.rgb: linear color; w: intensity.
    TSVec4f colorAndIntensity = TSVec4f(0.0f);
    // Point: constant/linear/quadratic. Area: width/height/two-sided.
    TSVec4f parameters = TSVec4f(0.0f);
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
    TSVec4f baseColorFactorAndTexture;
    // x: scalar HDR emission; positive values use the unlit shading model.
    TSVec4f materialEmission;
    TSVec4f materialRimColorAndStrength;
    // x: rim power.
    TSVec4f materialRimParams;
    TSVec4f lightMeta;
    std::array<GpuLightData, MaxSceneLights> lights;
    TSMat4f lightViewProj;
    // x: slope bias, y: minimum bias, z: shadow-light index, w: strength.
    TSVec4f shadowParams;
    // x: adaptive PCSS, y: half-resolution HBAO, z: SSDO, w: SSR.
    TSVec4f advancedLightingParams;
    // x: light size in shadow UV, y: maximum filter radius.
    TSVec4f pcssParams;
    // x: screen radius in pixels, y: intensity, z: world radius, w: bias.
    TSVec4f ssaoParams;
    // x: screen radius in pixels, y: intensity, z: world radius, w: bias.
    TSVec4f ssdoParams;
    // x: max distance, y: step size, z: thickness, w: intensity.
    TSVec4f ssrParams;
    TSMat4f previousView;
    TSMat4f previousProj;
    TSMat4f previousModel;
    // x: enabled and valid, y: history weight.
    TSVec4f taaParams;
};

struct SkyUniformBufferObject {
    TSMat4f view;
    TSMat4f proj;
};

struct PassResources {
    std::shared_ptr<PipelinePass> logicalPass;
    std::vector<std::shared_ptr<Pass>> rhiPasses;
    std::shared_ptr<Pipeline> gpuPipeline;
    std::array<std::shared_ptr<Pipeline>, 8> postProcessPipelines{};
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<DescriptorPool> descriptorPool;
    std::vector<DescriptorSet> descriptorSets;
    std::vector<std::shared_ptr<Buffer>> uniformBuffers;
    uint32_t descriptorSetsPerFrame = 1;
    bool usesSwapchain = false;
};

struct MaterialPbrValues {
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
};

MaterialPbrValues resolveMaterialPbr(const std::shared_ptr<Material>& material) {
    if (!material) {
        return {};
    }
    return {
        std::clamp(material->getFloat("metallic", 0.0f), 0.0f, 1.0f),
        std::clamp(material->getFloat("roughness", 1.0f), 0.04f, 1.0f),
        std::clamp(material->getFloat("ao", 1.0f), 0.0f, 1.0f)
    };
}

TSMat4f orthographicProjection(
    float left,
    float right,
    float bottom,
    float top,
    float nearPlane,
    float farPlane) {
    TSMat4f result(1.0f);
    result[0][0] = 2.0f / (right - left);
    result[1][1] = 2.0f / (top - bottom);
    result[2][2] = -1.0f / (farPlane - nearPlane);
    result[3][0] = -(right + left) / (right - left);
    result[3][1] = -(top + bottom) / (top - bottom);
    result[3][2] = -nearPlane / (farPlane - nearPlane);
    return result;
}

bool passHasExecutableWork(const PipelinePass& pass) {
    switch (pass.getExecution()) {
    case PipelinePassExecution::Fullscreen:
        return true;
    case PipelinePassExecution::Skybox:
        return !pass.getObjects().empty();
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

float halton(uint64_t index, uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
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
    case DepthTestMode::Equal:
        return CompareEqual;
    case DepthTestMode::Greater:
        return CompareGreater;
    case DepthTestMode::NotEqual:
        return CompareNotEqual;
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
    TSVec4f baseColorFactor = material.getVec4("baseColorFactor", TSVec4f(1.0f));
    float baseColor[3] = {baseColorFactor.x, baseColorFactor.y, baseColorFactor.z};
    if (ImGui::ColorEdit3("Base Color", baseColor)) {
        material.setVec4(
            "baseColorFactor",
            TSVec4f(baseColor[0], baseColor[1], baseColor[2], baseColorFactor.w));
    }

    float metallic = material.getFloat("metallic", 0.0f);
    float roughness = material.getFloat("roughness", 1.0f);
    float ao = material.getFloat("ao", 1.0f);
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
        material.setFloat("metallic", metallic);
    }
    if (ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f)) {
        material.setFloat("roughness", roughness);
    }
    if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) {
        material.setFloat("ao", ao);
    }
    float emissiveIntensity = material.getFloat("emissiveIntensity", 0.0f);
    if (ImGui::SliderFloat("Emission", &emissiveIntensity, 0.0f, 50.0f)) {
        material.setFloat("emissiveIntensity", emissiveIntensity);
    }
    float rimStrength = material.getFloat("rimStrength", 0.0f);
    float rimPower = material.getFloat("rimPower", 3.0f);
    TSVec3f rimColorValue = material.getVec3("rimColor", TSVec3f(1.0f));
    float rimColor[3] = {rimColorValue.x, rimColorValue.y, rimColorValue.z};
    if (ImGui::SliderFloat("Rim Strength", &rimStrength, 0.0f, 5.0f)) {
        material.setFloat("rimStrength", rimStrength);
    }
    if (ImGui::SliderFloat("Rim Power", &rimPower, 0.25f, 12.0f)) {
        material.setFloat("rimPower", rimPower);
    }
    if (ImGui::ColorEdit3("Rim Color", rimColor)) {
        material.setVec3("rimColor", TSVec3f(rimColor[0], rimColor[1], rimColor[2]));
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
    bool environmentLightingEnabled = false;
    std::vector<SkyboxVariant> skyboxVariants;
    int selectedSkyboxIndex = 0;
    std::string activeSkyboxName;
    std::shared_ptr<Buffer> skyboxVertexBuffer;
    std::shared_ptr<Buffer> skyboxIndexBuffer;
    uint32_t skyboxIndexCount = 0;
    bool loggedSkyboxDrawState = false;
    bool loggedSubmeshMaterialBindings = false;
    int selectedPipelineIndex = 1;
    std::string debugOutputResource;
    int bodyUvMode = 1;
    int hairUvMode = 1;
    int faceUvMode = 1;
    float shadowSlopeBias = 0.003f;
    float shadowMinimumBias = 0.0005f;
    float shadowStrength = 1.0f;
    bool bloomEnabled = true;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.25f;
    float bloomRadius = 1.0f;
    float exposure = 1.0f;
    bool taaEnabled = true;
    // This simple implementation combines the current and immediately
    // preceding jittered lighting result rather than recursively filtering an
    // unbounded history, so an even default weight is the least biased.
    float taaHistoryWeight = 0.5f;
    bool taaHistoryValid = false;
    uint64_t taaFrameIndex = 0;
    TSMat4f previousView = TSMat4f(1.0f);
    TSMat4f previousFlippedProjection = TSMat4f(1.0f);
    TSMat4f previousUnflippedProjection = TSMat4f(1.0f);
    std::unordered_map<const Object*, TSMat4f> previousModelMatrices;
    std::vector<std::vector<std::string>> gpuTimingNamesPerFrame;
    std::vector<std::pair<std::string, double>> gpuPassTimings;
    Object* animatedTaffy = nullptr;
    TSVec3f taffyBaseRotation = TSVec3f(0.0f);
    float taffyYawOffset = 0.0f;
    std::chrono::steady_clock::time_point lastTaffyAnimationTime{};
    bool pcssEnabled = true;
    float pcssLightSize = 0.018f;
    float pcssMaxFilterRadius = 0.04f;
    bool ssaoEnabled = true;
    float ssaoRadiusPixels = 12.0f;
    float ssaoIntensity = 1.0f;
    float ssaoWorldRadius = 0.75f;
    float ssaoBias = 0.02f;
    bool ssdoEnabled = false;
    float ssdoRadiusPixels = 18.0f;
    float ssdoIntensity = 0.18f;
    float ssdoWorldRadius = 1.25f;
    float ssdoBias = 0.02f;
    bool ssrEnabled = false;
    float ssrMaxDistance = 8.0f;
    float ssrStepSize = 0.05f;
    float ssrThickness = 0.25f;
    float ssrIntensity = 0.65f;
    bool outlineEnabled = true;
    float outlineThreshold = 0.12f;
    float outlineThickness = 1.0f;
    float outlineStrength = 0.85f;
    float outlineSoftness = 0.05f;
    TSVec3f outlineColor = TSVec3f(0.02f, 0.015f, 0.02f);
    float uvOffset[2] = {0.0f, 0.0f};
    float uvScale[2] = {1.0f, 1.0f};
    std::unique_ptr<Tasrovy::UI::UIOverlay> ui;
    std::unique_ptr<ResourceMonitor> resourceMonitor;
    uint32_t maxFramesInFlight = 0;

    RenderState(Tasrovy::Windowing::Window& window, uint32_t maxFrames)
        : maxFramesInFlight(maxFrames) {
        device = Device::createForWindow(window, maxFrames);
        commandList = CommandList::create();
        ui = device->createUIOverlay(window);
        resourceMonitor = std::make_unique<ResourceMonitor>();
        gpuTimingNamesPerFrame.resize(maxFrames);
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
            if (renderState_->resourceMonitor) {
                renderState_->resourceMonitor->draw(
                    renderState_->device ? renderState_->device->getDeferredDeletionCount() : 0,
                    renderState_->gpuPassTimings);
            }
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

    if (ImGui::CollapsingHeader("UV Settings")) {
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

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Slope Bias", &state.shadowSlopeBias, 0.0f, 0.02f, "%.5f");
        ImGui::SliderFloat("Minimum Bias", &state.shadowMinimumBias, 0.0f, 0.01f, "%.5f");
        ImGui::SliderFloat("Strength", &state.shadowStrength, 0.0f, 1.0f);
        ImGui::TextDisabled("2048 x 2048; adaptive blocker search and edge filtering");
    }

    if (ImGui::CollapsingHeader("Advanced Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Adaptive PCSS", &state.pcssEnabled);
        if (state.pcssEnabled) {
            ImGui::SliderFloat("PCSS Light Size", &state.pcssLightSize, 0.001f, 0.08f, "%.4f");
            ImGui::SliderFloat(
                "PCSS Max Radius", &state.pcssMaxFilterRadius, 0.002f, 0.12f, "%.4f");
        }

        ImGui::Checkbox("HBAO", &state.ssaoEnabled);
        if (state.ssaoEnabled) {
            ImGui::SliderFloat("HBAO Screen Radius", &state.ssaoRadiusPixels, 2.0f, 64.0f);
            ImGui::SliderFloat("HBAO World Radius", &state.ssaoWorldRadius, 0.05f, 5.0f);
            ImGui::SliderFloat("HBAO Intensity", &state.ssaoIntensity, 0.0f, 4.0f);
            ImGui::SliderFloat("HBAO Bias", &state.ssaoBias, 0.0f, 0.2f, "%.4f");
        }

        ImGui::Checkbox("SSDO", &state.ssdoEnabled);
        if (state.ssdoEnabled) {
            ImGui::SliderFloat("SSDO Screen Radius", &state.ssdoRadiusPixels, 2.0f, 64.0f);
            ImGui::SliderFloat("SSDO World Radius", &state.ssdoWorldRadius, 0.05f, 5.0f);
            ImGui::SliderFloat("SSDO Intensity", &state.ssdoIntensity, 0.0f, 2.0f);
            ImGui::SliderFloat("SSDO Bias", &state.ssdoBias, 0.0f, 0.2f, "%.4f");
        }

        ImGui::Checkbox("SSR", &state.ssrEnabled);
        if (state.ssrEnabled) {
            ImGui::SliderFloat("SSR Max Distance", &state.ssrMaxDistance, 0.5f, 30.0f);
            ImGui::SliderFloat("SSR Step Size", &state.ssrStepSize, 0.02f, 1.0f);
            ImGui::SliderFloat("SSR Thickness", &state.ssrThickness, 0.01f, 1.0f);
            ImGui::SliderFloat("SSR Intensity", &state.ssrIntensity, 0.0f, 1.0f);
        }
    }

    if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("TAA", &state.taaEnabled)) {
            state.taaHistoryValid = false;
            state.previousModelMatrices.clear();
        }
        if (state.taaEnabled) {
            ImGui::SliderFloat(
                "TAA History Weight", &state.taaHistoryWeight, 0.0f, 0.95f);
        }
        ImGui::Separator();
        ImGui::Checkbox("Bloom", &state.bloomEnabled);
        ImGui::SliderFloat("Bloom Threshold", &state.bloomThreshold, 0.0f, 10.0f);
        ImGui::SliderFloat("Bloom Intensity", &state.bloomIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Bloom Radius", &state.bloomRadius, 0.25f, 4.0f);
        ImGui::SliderFloat("Exposure", &state.exposure, 0.05f, 5.0f);
        ImGui::Separator();
        ImGui::Checkbox("Normal Outline", &state.outlineEnabled);
        if (state.outlineEnabled) {
            ImGui::SliderFloat("Outline Threshold", &state.outlineThreshold, 0.001f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &state.outlineThickness, 0.5f, 5.0f);
            ImGui::SliderFloat("Outline Strength", &state.outlineStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Softness", &state.outlineSoftness, 0.001f, 0.5f);
            float outlineColor[3] = {
                state.outlineColor.x, state.outlineColor.y, state.outlineColor.z
            };
            if (ImGui::ColorEdit3("Outline Color", outlineColor)) {
                state.outlineColor = TSVec3f(
                    outlineColor[0], outlineColor[1], outlineColor[2]);
            }
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
    uint64_t appliedResizeGeneration = 0;
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
        if (windowSizeChanged || renderState_->device->isSwapchainRebuildRequired()) {
            if (!renderState_->device->recreateSwapchain(
                    static_cast<uint32_t>(framebuffer.width),
                    static_cast<uint32_t>(framebuffer.height))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            appliedResizeGeneration = framebuffer.resizeGeneration;
            if (auto* cam = scene->getPrimaryCamera()) {
                cam->setAspect(
                    static_cast<float>(renderState_->device->getSwapchainWidth()) /
                    static_cast<float>(renderState_->device->getSwapchainHeight()));
            }
            // Every deferred target, Hi-Z mip, pass extent, viewport and
            // descriptor that depends on the swapchain must be rebuilt using
            // the new extent before another frame is recorded.
            processScene(scene);
        }

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
        pipeline = DeferredPipeline::create();
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
    state.taaHistoryValid = false;
    state.taaFrameIndex = 0;
    state.previousModelMatrices.clear();
    for (auto& timingNames : state.gpuTimingNamesPerFrame) {
        timingNames.clear();
    }
    state.gpuPassTimings.clear();
    state.animatedTaffy = nullptr;
    state.taffyYawOffset = 0.0f;
    state.lastTaffyAnimationTime = {};

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

    state.environmentLightingEnabled = !preferredSkyboxPath.empty();
    // The deferred-lighting descriptor always contains the IBL bindings.
    // Prepare a valid environment even for closed scenes, then disable its
    // lighting contribution through the UBO when no Skybox object is present.
    prepareSkyboxVariants(preferredSkyboxPath);

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
            if (pass->getName() == "PostProcessing") {
                for (uint32_t permutation = 0; permutation < 8u; ++permutation) {
                    PipelineDesc permutationDesc = pipelineDesc;
                    permutationDesc.fragShaderPath =
                        "res\\Shaders\\Bin\\deferred_postprocess_" +
                        std::to_string(permutation) + "_frag.spv";
                    resources.postProcessPipelines[permutation] =
                        device.createGraphicsPipeline(permutationDesc);
                }
                // SSR off, Bloom and Outline on.
                resources.gpuPipeline = resources.postProcessPipelines[6];
            } else {
                resources.gpuPipeline = device.createGraphicsPipeline(pipelineDesc);
            }
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
    const auto completedGpuDurations = device.consumeGpuTimestampDurations();
    state.gpuPassTimings.clear();
    if (frameIdx < state.gpuTimingNamesPerFrame.size()) {
        const auto& completedNames = state.gpuTimingNamesPerFrame[frameIdx];
        const size_t completedCount = std::min(
            completedNames.size(), completedGpuDurations.size());
        state.gpuPassTimings.reserve(completedCount);
        for (size_t timingIndex = 0; timingIndex < completedCount; ++timingIndex) {
            state.gpuPassTimings.emplace_back(
                completedNames[timingIndex], completedGpuDurations[timingIndex]);
        }
        state.gpuTimingNamesPerFrame[frameIdx].clear();
    }
    device.beginGpuTimestampFrame(
        cmdList, static_cast<uint32_t>(state.passes.size() * 2u));
    std::unordered_map<const Object*, TSMat4f> currentModelMatrices;
    currentModelMatrices.reserve(state.previousModelMatrices.size() + 4u);
    bool drawUI = state.ui && device.beginUIFrame(*state.ui);

    // Run the TAA comparison animation on the render thread so transform
    // writes cannot race command generation on the RHI/render path.
    const auto animationNow = std::chrono::steady_clock::now();
    Object* taffy = scene.findObject("Taffy");
    if (taffy != state.animatedTaffy) {
        state.animatedTaffy = taffy;
        state.taffyBaseRotation = taffy ? taffy->getRotationEuler() : TSVec3f(0.0f);
        state.taffyYawOffset = 0.0f;
        state.lastTaffyAnimationTime = animationNow;
    } else if (taffy) {
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(
                animationNow - state.lastTaffyAnimationTime).count(),
            0.0f,
            0.1f);
        const float taffyYawRadiansPerSecond = pi<float>() * 0.25f;
        state.taffyYawOffset = std::fmod(
            state.taffyYawOffset + taffyYawRadiansPerSecond * deltaSeconds,
            2.0f * pi<float>());
        TSVec3f animatedRotation = state.taffyBaseRotation;
        animatedRotation.y += state.taffyYawOffset;
        taffy->setRotation(animatedRotation);
        state.lastTaffyAnimationTime = animationNow;
    }

    auto* cam = scene.getPrimaryCamera();
    TSMat4f viewMat = cam->getViewMatrix();
    TSMat4f unflippedProjMat = cam->getProjectionMatrix();
    if (state.taaEnabled) {
        const uint64_t jitterIndex = state.taaFrameIndex % 8u + 1u;
        const float jitterX = halton(jitterIndex, 2u) - 0.5f;
        const float jitterY = halton(jitterIndex, 3u) - 0.5f;
        unflippedProjMat[2][0] +=
            jitterX * 2.0f / static_cast<float>(device.getSwapchainWidth());
        unflippedProjMat[2][1] +=
            jitterY * 2.0f / static_cast<float>(device.getSwapchainHeight());
    }
    TSMat4f projMat = unflippedProjMat;
    projMat[1][1] *= -1;

    TSVec3f lightDir(-0.5f, -1.0f, -0.8f);
    TSVec3f lightColor(1.0f);
    float lightIntensity = 10.0f;
    if (!scene.getLights().empty() && scene.getLights()[0]) {
        const auto* firstLight = scene.getLights()[0].get();
        lightDir = firstLight->getDirection();
        lightColor = firstLight->getColor();
        lightIntensity = firstLight->getIntensity();
    }

    // A single shadow map currently follows one light. Prefer the area light
    // used by the Cornell scene, then fall back to the first directional light.
    const Light* shadowLight = nullptr;
    for (const auto& light : scene.getLights()) {
        if (light && dynamic_cast<const AreaLight*>(light.get())) {
            shadowLight = light.get();
            break;
        }
    }
    if (!shadowLight) {
        for (const auto& light : scene.getLights()) {
            if (light && dynamic_cast<const DirectionalLight*>(light.get())) {
                shadowLight = light.get();
                break;
            }
        }
    }

    std::array<GpuLightData, MaxSceneLights> gpuLights{};
    uint32_t gpuLightCount = 0;
    int32_t shadowLightIndex = -1;
    for (const auto& lightPtr : scene.getLights()) {
        if (!lightPtr || gpuLightCount >= MaxSceneLights) {
            continue;
        }

        auto& gpuLight = gpuLights[gpuLightCount];
        gpuLight.colorAndIntensity = TSVec4f(lightPtr->getColor(), lightPtr->getIntensity());
        if (const auto* directional = dynamic_cast<const DirectionalLight*>(lightPtr.get())) {
            gpuLight.positionAndType = TSVec4f(0.0f, 0.0f, 0.0f, 0.0f);
            gpuLight.directionAndRange = TSVec4f(normalize(directional->getDirection()), 0.0f);
        } else if (const auto* point = dynamic_cast<const PointLight*>(lightPtr.get())) {
            gpuLight.positionAndType = TSVec4f(point->getPosition(), 1.0f);
            gpuLight.parameters = TSVec4f(
                point->getConstant(), point->getLinear(), point->getQuadratic(), 0.0f);
        } else if (const auto* area = dynamic_cast<const AreaLight*>(lightPtr.get())) {
            gpuLight.positionAndType = TSVec4f(area->getPosition(), 2.0f);
            gpuLight.directionAndRange = TSVec4f(normalize(area->getDirection()), 0.0f);
            gpuLight.parameters = TSVec4f(
                area->getWidth(), area->getHeight(), area->isTwoSided() ? 1.0f : 0.0f, 0.0f);
        } else if (const auto* spot = dynamic_cast<const SpotLight*>(lightPtr.get())) {
            // Until a dedicated cone model is added, keep spot lights usable
            // as positional lights in the deferred path.
            gpuLight.positionAndType = TSVec4f(spot->getPosition(), 1.0f);
            gpuLight.directionAndRange = TSVec4f(normalize(spot->getDirection()), spot->getCutoff());
            gpuLight.parameters = TSVec4f(1.0f, 0.09f, 0.032f, 0.0f);
        } else {
            continue;
        }
        if (lightPtr.get() == shadowLight) {
            shadowLightIndex = static_cast<int32_t>(gpuLightCount);
        }
        ++gpuLightCount;
    }

    TSMat4f shadowView(1.0f);
    TSMat4f shadowProjection(1.0f);
    if (const auto* area = dynamic_cast<const AreaLight*>(shadowLight)) {
        const TSVec3f direction = normalize(area->getDirection());
        const TSVec3f up = std::abs(direction.y) > 0.95f
            ? TSVec3f(0.0f, 0.0f, 1.0f)
            : TSVec3f(0.0f, 1.0f, 0.0f);
        shadowView = lookAt(area->getPosition(), area->getPosition() + direction, up);
        shadowProjection = orthographicProjection(-3.1f, 3.1f, -3.1f, 3.1f, 0.05f, 12.0f);
    } else if (const auto* directional = dynamic_cast<const DirectionalLight*>(shadowLight)) {
        const TSVec3f direction = normalize(directional->getDirection());
        const TSVec3f center(0.0f, 2.5f, 0.0f);
        const TSVec3f eye = center - direction * 10.0f;
        const TSVec3f up = std::abs(direction.y) > 0.95f
            ? TSVec3f(0.0f, 0.0f, 1.0f)
            : TSVec3f(0.0f, 1.0f, 0.0f);
        shadowView = lookAt(eye, center, up);
        shadowProjection = orthographicProjection(-6.5f, 6.5f, -6.5f, 6.5f, 0.1f, 25.0f);
    }
    // Vulkan NDC has an inverted framebuffer Y relative to the math helpers.
    shadowProjection[1][1] *= -1.0f;
    const TSMat4f shadowViewProjection = shadowProjection * shadowView;

    const auto populateExtendedMaterialAndLights =
        [&](UniformBufferObject& ubo, const std::shared_ptr<Material>& material) {
            const TSVec4f baseColorFactor = material
                ? material->getVec4("baseColorFactor", TSVec4f(1.0f))
                : TSVec4f(1.0f);
            const bool hasBaseColorTexture = material &&
                !material->getTexture(MaterialTextureSemantic::BaseColor).empty();
            ubo.baseColorFactorAndTexture = TSVec4f(
                baseColorFactor.x,
                baseColorFactor.y,
                baseColorFactor.z,
                hasBaseColorTexture ? 1.0f : 0.0f);
            ubo.materialEmission = TSVec4f(
                material ? material->getFloat("emissiveIntensity", 0.0f) : 0.0f,
                0.0f,
                0.0f,
                0.0f);
            const TSVec3f rimColor = material
                ? material->getVec3("rimColor", TSVec3f(1.0f))
                : TSVec3f(1.0f);
            ubo.materialRimColorAndStrength = TSVec4f(
                rimColor,
                material ? material->getFloat("rimStrength", 0.0f) : 0.0f);
            ubo.materialRimParams = TSVec4f(
                material ? material->getFloat("rimPower", 3.0f) : 3.0f,
                0.0f,
                0.0f,
                0.0f);
            ubo.lightMeta = TSVec4f(
                static_cast<float>(gpuLightCount),
                state.environmentLightingEnabled ? 1.0f : 0.0f,
                0.0f,
                0.0f);
            ubo.lights = gpuLights;
            ubo.lightViewProj = transpose(shadowViewProjection);
            ubo.shadowParams = TSVec4f(
                state.shadowSlopeBias,
                state.shadowMinimumBias,
                static_cast<float>(shadowLightIndex),
                state.shadowStrength);
            ubo.advancedLightingParams = TSVec4f(
                state.pcssEnabled ? 1.0f : 0.0f,
                state.ssaoEnabled ? 1.0f : 0.0f,
                state.ssdoEnabled ? 1.0f : 0.0f,
                state.ssrEnabled ? 1.0f : 0.0f);
            ubo.pcssParams = TSVec4f(
                state.pcssLightSize, state.pcssMaxFilterRadius, 0.0f, 0.0f);
            ubo.ssaoParams = TSVec4f(
                state.ssaoRadiusPixels,
                state.ssaoIntensity,
                state.ssaoWorldRadius,
                state.ssaoBias);
            ubo.ssdoParams = TSVec4f(
                state.ssdoRadiusPixels,
                state.ssdoIntensity,
                state.ssdoWorldRadius,
                state.ssdoBias);
            ubo.ssrParams = TSVec4f(
                state.ssrMaxDistance,
                state.ssrStepSize,
                state.ssrThickness,
                state.ssrIntensity);
        };

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
                pass.logicalPass->getName() == "PostProcessing" &&
                !state.debugOutputResource.empty();
            ubo.roughnessAo = TSVec4f(
                1.0f,
                1.0f,
                debugPostProcess ? 1.0f : 0.0f,
                state.bloomRadius);
            const bool isPostProcessingPass =
                pass.logicalPass->getType() == PipelinePassType::PostProcess;
            ubo.uvTransform = isPostProcessingPass
                ? TSVec4f(
                    state.bloomEnabled ? 1.0f : 0.0f,
                    state.bloomThreshold,
                    state.bloomIntensity,
                    state.exposure)
                : TSVec4f(
                    state.uvScale[0], state.uvScale[1],
                    state.uvOffset[0], state.uvOffset[1]);
            if (pass.logicalPass->getName() == "PostProcessing") {
                ubo.lightDir = TSVec4f(
                    state.outlineColor,
                    state.outlineEnabled ? 1.0f : 0.0f);
                ubo.lightColor = TSVec4f(
                    state.outlineThreshold,
                    state.outlineThickness,
                    state.outlineStrength,
                    state.outlineSoftness);
            }
            populateExtendedMaterialAndLights(ubo, nullptr);
            const bool isTaaPass =
                pass.logicalPass->getName() == "PostProcessing";
            const bool taaHistoryUsable =
                isTaaPass && state.taaEnabled && state.taaHistoryValid &&
                !debugPostProcess;
            ubo.previousView = transpose(
                state.taaHistoryValid ? state.previousView : viewMat);
            ubo.previousProj = transpose(
                state.taaHistoryValid ? state.previousFlippedProjection : projMat);
            ubo.previousModel = transpose(TSMat4f(1.0f));
            ubo.taaParams = TSVec4f(
                taaHistoryUsable ? 1.0f : 0.0f,
                state.taaHistoryWeight,
                0.0f,
                0.0f);
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
                uint32_t sampledFrame = frameIdx;
                if ((input.slot == "taaHistoryColor" ||
                     input.slot == "taaHistoryDepth") &&
                    state.taaHistoryValid) {
                    sampledFrame =
                        (frameIdx + maxFramesInFlight_ - 1u) % maxFramesInFlight_;
                }
                if (found == state.renderTextures.end() ||
                    sampledFrame >= found->second.size() ||
                    !found->second[sampledFrame]) {
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
                    found->second[sampledFrame]
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

            const auto pbr = resolveMaterialPbr(material);

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
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), pbr.metallic);
            ubo.roughnessAo = TSVec4f(
                pbr.roughness,
                pbr.ao,
                0.0f,
                static_cast<float>(state.bodyUvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            populateExtendedMaterialAndLights(ubo, material);
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

            const auto pbr = resolveMaterialPbr(material);

            UniformBufferObject ubo{};
            ubo.model = transpose(drawObject ? drawObject->getModelMatrix() : TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), pbr.metallic);
            ubo.roughnessAo = TSVec4f(
                pbr.roughness,
                pbr.ao,
                0.0f,
                static_cast<float>(state.bodyUvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            populateExtendedMaterialAndLights(ubo, material);
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

            const auto material = submeshMaterial ? submeshMaterial : object.getMaterial();
            const auto pbr = resolveMaterialPbr(material);

            UniformBufferObject ubo{};
            const TSMat4f currentModel = object.getModelMatrix();
            if (pass.logicalPass->getName() == "GBuffer") {
                currentModelMatrices[&object] = currentModel;
            }
            ubo.model = transpose(currentModel);
            const bool isShadowPass =
                pass.logicalPass->getType() == PipelinePassType::Shadow;
            ubo.view = transpose(isShadowPass ? shadowView : viewMat);
            ubo.proj = transpose(
                isShadowPass
                    ? shadowProjection
                    : (object.getFlipProjectionY() ? projMat : unflippedProjMat));
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), pbr.metallic);
            const int uvMode = selectMaterialUvMode(
                submeshMaterialName,
                state.bodyUvMode,
                state.hairUvMode,
                state.faceUvMode);
            ubo.roughnessAo = TSVec4f(
                pbr.roughness,
                pbr.ao,
                0.0f,
                static_cast<float>(uvMode));
            ubo.uvTransform = TSVec4f(
                state.uvScale[0], state.uvScale[1],
                state.uvOffset[0], state.uvOffset[1]);
            populateExtendedMaterialAndLights(ubo, material);
            ubo.previousView = transpose(
                state.taaHistoryValid ? state.previousView : viewMat);
            const TSMat4f& previousObjectProjection = object.getFlipProjectionY()
                ? state.previousFlippedProjection
                : state.previousUnflippedProjection;
            ubo.previousProj = transpose(
                state.taaHistoryValid
                    ? previousObjectProjection
                    : (object.getFlipProjectionY() ? projMat : unflippedProjMat));
            const auto previousModel = state.previousModelMatrices.find(&object);
            ubo.previousModel = transpose(
                state.taaHistoryValid && previousModel != state.previousModelMatrices.end()
                    ? previousModel->second
                    : currentModel);
            ubo.taaParams = TSVec4f(0.0f);
            pass.uniformBuffers[descriptorIndex]->setData(&ubo, sizeof(ubo));

            std::vector<DescriptorWriteDesc> writes = {
                {0, DescriptorResourceType::UniformBuffer, pass.uniformBuffers[descriptorIndex]}
            };

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
    uint32_t gpuTimestampCount = 0;
    for (auto& pass : state.passes) {
        const std::string& passName = pass.logicalPass->getName();
        const bool isHiZPass =
            passName == "HiZHalf" ||
            passName == "HiZQuarter" ||
            passName == "HiZEighth" ||
            passName == "HiZSixteenth";
        // Hi-Z exists exclusively for SSR. Avoid four fullscreen draws and
        // their attachment transitions while SSR is disabled. Enabling SSR
        // rebuilds every level earlier in this same command list.
        if (isHiZPass && !state.ssrEnabled) {
            continue;
        }
        if (passName == "HBAO" && !state.ssaoEnabled) {
            continue;
        }
        if (passName == "BloomLowRes" && !state.bloomEnabled) {
            continue;
        }
        if (frameIdx >= pass.rhiPasses.size() || !pass.rhiPasses[frameIdx]) {
            continue;
        }
        const auto& activeRhiPass = pass.rhiPasses[frameIdx];
        if (!pass.usesSwapchain && !pass.gpuPipeline) {
            continue;
        }

        const auto& passDesc = activeRhiPass->getDesc();
        device.writeGpuTimestamp(cmdList, gpuTimestampCount++, true);
        if (frameIdx < state.gpuTimingNamesPerFrame.size()) {
            state.gpuTimingNamesPerFrame[frameIdx].push_back(passName);
        }
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

        std::shared_ptr<Pipeline> activeGpuPipeline = pass.gpuPipeline;
        if (passName == "PostProcessing") {
            const uint32_t permutation =
                (state.ssrEnabled ? 1u : 0u) |
                (state.bloomEnabled ? 2u : 0u) |
                (state.outlineEnabled ? 4u : 0u);
            if (pass.postProcessPipelines[permutation]) {
                activeGpuPipeline = pass.postProcessPipelines[permutation];
            }
        }
        if (activeGpuPipeline) {
            cmdList.bindPipeline(
                activeGpuPipeline->getNativePipeline(),
                activeGpuPipeline->getNativeLayout());

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
        device.writeGpuTimestamp(cmdList, gpuTimestampCount++, false);
    }

    if (swapchainPassOpen) {
        device.endFrameRenderPass(cmdList);
        if (drawUI) {
            device.renderUI(*state.ui, cmdList);
        }
    }

    device.endGpuTimestampFrame(gpuTimestampCount);
    state.loggedSubmeshMaterialBindings = true;
    device.endFrame();

    // Queue submission order guarantees that the preceding frame's color and
    // depth attachments are complete before the next frame samples them.
    // Publish the exact jittered matrices used for this submitted frame only
    // after recording and submission have succeeded.
    state.previousView = viewMat;
    state.previousFlippedProjection = projMat;
    state.previousUnflippedProjection = unflippedProjMat;
    state.previousModelMatrices = std::move(currentModelMatrices);
    state.taaHistoryValid = true;
    ++state.taaFrameIndex;
}

} // namespace Tasrovy::RHI
