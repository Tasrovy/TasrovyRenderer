#include "SceneGPUResources.h"

#include "SkyboxGeometry.h"
#include "../RHI/Buffer.h"
#include "../RHI/CommandList.h"
#include "../RHI/FrameScheduler.h"
#include "../RHI/Image.h"
#include "../assets/RenderAssetFactory.h"
#include "../render/Material.h"
#include "../render/Mesh.h"
#include "../render/Object.h"
#include "Logger.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace Tasrovy::Renderer {

using namespace Tasrovy::Render;
using namespace Tasrovy::RHI;

namespace {

struct SkyboxCandidate {
    std::string name;
    std::string path;
};

ImageUploadDesc loadTextureUpload(
    const std::string& path,
    uint32_t format,
    bool generateMipmaps) {
    const auto source = Tasrovy::Assets::RenderAssetFactory::decodeTexture(path);
    ImageUploadDesc upload{};
    upload.width = source.width;
    upload.height = source.height;
    upload.channels = 4;
    upload.format = format;
    upload.generateMipmaps = generateMipmaps;
    upload.pixels = source.pixels;
    return upload;
}

ImageUploadDesc loadCubemapUpload(
    const std::string& directory,
    uint32_t format = FormatRGBA8Srgb) {
    const auto source =
        Tasrovy::Assets::RenderAssetFactory::decodeCubemap(directory);
    ImageUploadDesc upload{};
    upload.width = source.width;
    upload.height = source.height;
    upload.channels = source.channels;
    upload.arrayLayers = source.arrayLayers;
    upload.format = format;
    upload.generateMipmaps = false;
    upload.cubemap = true;
    upload.pixels = source.pixels;
    return upload;
}

std::string normalizePathForAssets(const std::filesystem::path& path) {
    return path.generic_string();
}

bool hasCubemapFaces(const std::filesystem::path& directory) {
    static const char* faces[] = {
        "right.png",
        "left.png",
        "top.png",
        "bottom.png",
        "front.png",
        "back.png"
    };
    for (const auto* face : faces) {
        if (!std::filesystem::exists(directory / face)) {
            return false;
        }
    }
    return true;
}

std::vector<SkyboxCandidate> discoverSkyboxCandidates(
    const std::string& preferredPath) {
    std::vector<SkyboxCandidate> candidates;
    const std::filesystem::path resourcePath("res/Skyboxes");
    if (std::filesystem::exists(resourcePath)) {
        for (const auto& entry :
             std::filesystem::directory_iterator(resourcePath)) {
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
        const auto normalized = normalizePathForAssets(preferred);
        const bool alreadyListed = std::any_of(
            candidates.begin(),
            candidates.end(),
            [&](const SkyboxCandidate& candidate) {
                return candidate.path == normalized;
            });
        if (!alreadyListed && hasCubemapFaces(preferred)) {
            candidates.push_back({preferred.filename().string(), normalized});
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

} // namespace

void SceneGPUResources::resetScene() {
    materialTextures_.clear();
    defaultMaterialTextures_.clear();
    meshes_.clear();
    skyboxVertexBuffer_.reset();
    skyboxIndexBuffer_.reset();
    skyboxIndexCount_ = 0;
}

void SceneGPUResources::rebuildMeshes(
    Device& device,
    FrameScheduler& scheduler,
    CommandList& commandList,
    Device::ResourceScope sceneScope,
    const std::vector<std::shared_ptr<Object>>& objects) {
    meshes_.clear();
    for (const auto& object : objects) {
        const auto mesh = object ? object->getMesh() : nullptr;
        if (!mesh || meshes_.contains(mesh.get())) {
            continue;
        }

        MeshGPUResources resources;
        const auto vertexSize =
            mesh->getVertices().size() * sizeof(MeshVertex);
        const auto indexSize =
            mesh->getIndices().size() * sizeof(uint32_t);
        resources.vertexBuffer = device.retainResource(
            sceneScope, device.createVertexBuffer(vertexSize));
        resources.indexBuffer = device.retainResource(
            sceneScope, device.createIndexBuffer(indexSize));
        resources.indexCount =
            static_cast<uint32_t>(mesh->getIndices().size());

        if (vertexSize > 0 && resources.vertexBuffer) {
            uploadBuffer(
                device,
                scheduler,
                commandList,
                *resources.vertexBuffer,
                mesh->getVertices().data(),
                vertexSize);
        }
        if (indexSize > 0 && resources.indexBuffer) {
            uploadBuffer(
                device,
                scheduler,
                commandList,
                *resources.indexBuffer,
                mesh->getIndices().data(),
                indexSize);
        }

        LOG_INFO(
            "SceneGPUResources: uploaded mesh '{}' vertices {} indices {}",
            object->getName(),
            mesh->getVertexCount(),
            mesh->getIndexCount());
        meshes_.emplace(mesh.get(), std::move(resources));
    }
}

void SceneGPUResources::ensureDefaultTexture(
    Device& device,
    Device::ResourceScope sceneScope,
    const MaterialTextureRequirement& requirement) {
    const auto key = defaultMaterialTextureCacheKey(requirement);
    if (defaultMaterialTextures_.contains(key)) {
        return;
    }
    defaultMaterialTextures_.emplace(
        key,
        device.retainResource(
            sceneScope,
            device.createSolidTexture(
                {
                    requirement.defaultColor.x,
                    requirement.defaultColor.y,
                    requirement.defaultColor.z,
                    requirement.defaultColor.w
                },
                isSRGB(requirement.colorSpace)
                    ? FormatRGBA8Srgb
                    : FormatRGBA8Unorm)));
}

void SceneGPUResources::ensureMaterialTextures(
    Device& device,
    Device::ResourceScope sceneScope,
    const std::shared_ptr<Material>& material,
    const std::vector<MaterialTextureRequirement>& requirements) {
    if (!material) {
        return;
    }
    for (const auto& requirement : requirements) {
        const auto* binding = material->resolveTexture(requirement);
        if (!binding || binding->path.empty()) {
            continue;
        }
        const auto cacheKey = materialTextureCacheKey(
            binding->path, requirement.colorSpace);
        if (materialTextures_.contains(cacheKey)) {
            continue;
        }
        materialTextures_[cacheKey] = device.retainResource(
            sceneScope,
            device.createTexture(loadTextureUpload(
                binding->path,
                isSRGB(requirement.colorSpace)
                    ? FormatRGBA8Srgb
                    : FormatRGBA8Unorm,
                true)));
    }
}

ResolvedMaterialTexture SceneGPUResources::resolveMaterialTexture(
    const std::shared_ptr<Material>& material,
    const MaterialTextureRequirement& requirement) const {
    const auto fallbackKey = defaultMaterialTextureCacheKey(requirement);
    const auto fallback = defaultMaterialTextures_.find(fallbackKey);
    const auto fallbackImage = fallback == defaultMaterialTextures_.end()
        ? nullptr
        : fallback->second;
    if (!material) {
        return {fallbackKey, fallbackImage};
    }

    const auto path = material->getTexture(requirement.slot);
    if (path.empty()) {
        return {fallbackKey, fallbackImage};
    }
    const auto key = materialTextureCacheKey(path, requirement.colorSpace);
    const auto found = materialTextures_.find(key);
    return found == materialTextures_.end()
        ? ResolvedMaterialTexture{fallbackKey, fallbackImage}
        : ResolvedMaterialTexture{key, found->second};
}

const MeshGPUResources* SceneGPUResources::findMesh(
    const Mesh& mesh) const {
    const auto found = meshes_.find(&mesh);
    return found == meshes_.end() ? nullptr : &found->second;
}

void SceneGPUResources::rebuildSkyboxGeometry(
    Device& device,
    FrameScheduler& scheduler,
    CommandList& commandList,
    Device::ResourceScope sceneScope,
    bool enabled) {
    skyboxVertexBuffer_.reset();
    skyboxIndexBuffer_.reset();
    skyboxIndexCount_ = 0;
    if (!enabled) {
        return;
    }

    const auto& vertices = getSkyboxVertices();
    const auto& indices = getSkyboxIndices();
    const auto vertexSize = vertices.size() * sizeof(SkyboxVertexData);
    const auto indexSize = indices.size() * sizeof(uint32_t);
    skyboxVertexBuffer_ = device.retainResource(
        sceneScope, device.createVertexBuffer(vertexSize));
    skyboxIndexBuffer_ = device.retainResource(
        sceneScope, device.createIndexBuffer(indexSize));
    skyboxIndexCount_ = static_cast<uint32_t>(indices.size());
    if (skyboxVertexBuffer_ && vertexSize > 0) {
        uploadBuffer(
            device,
            scheduler,
            commandList,
            *skyboxVertexBuffer_,
            vertices.data(),
            vertexSize);
    }
    if (skyboxIndexBuffer_ && indexSize > 0) {
        uploadBuffer(
            device,
            scheduler,
            commandList,
            *skyboxIndexBuffer_,
            indices.data(),
            indexSize);
    }
}

void SceneGPUResources::prepareSkyboxVariants(
    Device& device,
    Device::ResourceScope persistentScope,
    const std::string& preferredPath) {
    if (!iblFallbackCubemap_) {
        ImageUploadDesc neutralCube;
        neutralCube.width = 1;
        neutralCube.height = 1;
        neutralCube.channels = 4;
        neutralCube.arrayLayers = 6;
        neutralCube.format = FormatRGBA8Unorm;
        neutralCube.generateMipmaps = false;
        neutralCube.cubemap = true;
        neutralCube.pixels.resize(6u * 4u, 0u);
        for (size_t face = 0; face < 6; ++face) {
            neutralCube.pixels[face * 4u + 3u] = 255u;
        }
        iblFallbackCubemap_ = device.retainResource(
            persistentScope,
            device.createTexture(neutralCube));
    }
    if (!iblFallbackLut_) {
        iblFallbackLut_ = device.retainResource(
            persistentScope,
            device.createSolidTexture(
                {1.0f, 1.0f, 1.0f, 1.0f},
                FormatRGBA8Unorm));
    }

    if (skyboxVariants_.empty()) {
        const auto candidates = discoverSkyboxCandidates(preferredPath);
        skyboxVariants_.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            LOG_INFO(
                "SceneGPUResources: loading skybox '{}' from '{}'",
                candidate.name,
                candidate.path);
            auto cubemap = device.retainResource(
                persistentScope,
                device.createTexture(loadCubemapUpload(candidate.path)));
            if (!cubemap) {
                LOG_WARN(
                    "SceneGPUResources: failed to create skybox '{}'",
                    candidate.name);
                continue;
            }
            skyboxVariants_.push_back({
                candidate.name,
                candidate.path,
                std::move(cubemap)
            });
        }
    }

    if (skyboxVariants_.empty()) {
        skyCubemap_.reset();
        activeSkyboxName_.clear();
        selectedSkyboxIndex_ = 0;
        return;
    }

    if (!preferredPath.empty() && activeSkyboxName_.empty()) {
        const auto preferredNormalized =
            normalizePathForAssets(std::filesystem::path(preferredPath));
        for (size_t index = 0; index < skyboxVariants_.size(); ++index) {
            if (skyboxVariants_[index].path == preferredNormalized) {
                selectedSkyboxIndex_ = static_cast<int>(index);
                break;
            }
        }
    }
    selectSkybox(selectedSkyboxIndex_);
}

bool SceneGPUResources::selectSkybox(int index) {
    if (skyboxVariants_.empty()) {
        selectedSkyboxIndex_ = 0;
        skyCubemap_.reset();
        activeSkyboxName_.clear();
        return false;
    }
    if (index < 0 || index >= static_cast<int>(skyboxVariants_.size())) {
        index = 0;
    }
    const bool changed = index != selectedSkyboxIndex_ || !skyCubemap_;
    selectedSkyboxIndex_ = index;
    const auto& selected = skyboxVariants_[static_cast<size_t>(index)];
    skyCubemap_ = selected.cubemap;
    activeSkyboxName_ = selected.name;
    return changed;
}

const std::shared_ptr<Buffer>&
SceneGPUResources::skyboxVertexBuffer() const {
    return skyboxVertexBuffer_;
}

const std::shared_ptr<Buffer>&
SceneGPUResources::skyboxIndexBuffer() const {
    return skyboxIndexBuffer_;
}

const std::shared_ptr<Image>& SceneGPUResources::skyCubemap() const {
    return skyCubemap_;
}

const std::shared_ptr<Image>& SceneGPUResources::iblFallbackCubemap() const {
    return iblFallbackCubemap_;
}

const std::shared_ptr<Image>& SceneGPUResources::iblFallbackLut() const {
    return iblFallbackLut_;
}

const std::vector<SkyboxVariant>&
SceneGPUResources::skyboxVariants() const {
    return skyboxVariants_;
}

const std::string& SceneGPUResources::activeSkyboxName() const {
    return activeSkyboxName_;
}

int SceneGPUResources::selectedSkyboxIndex() const {
    return selectedSkyboxIndex_;
}

uint32_t SceneGPUResources::skyboxIndexCount() const {
    return skyboxIndexCount_;
}

size_t SceneGPUResources::meshCount() const {
    return meshes_.size();
}

size_t SceneGPUResources::materialTextureCount() const {
    return materialTextures_.size();
}

uint64_t SceneGPUResources::meshBufferBytes() const {
    uint64_t bytes = 0;
    for (const auto& [_, mesh] : meshes_) {
        if (mesh.vertexBuffer) {
            bytes += mesh.vertexBuffer->getSize();
        }
        if (mesh.indexBuffer) {
            bytes += mesh.indexBuffer->getSize();
        }
    }
    return bytes;
}

uint64_t SceneGPUResources::skyboxBufferBytes() const {
    uint64_t bytes = 0;
    if (skyboxVertexBuffer_) {
        bytes += skyboxVertexBuffer_->getSize();
    }
    if (skyboxIndexBuffer_) {
        bytes += skyboxIndexBuffer_->getSize();
    }
    return bytes;
}

bool SceneGPUResources::isSRGB(
    MaterialTextureColorSpace colorSpace) {
    return colorSpace == MaterialTextureColorSpace::SRGB;
}

std::string SceneGPUResources::materialTextureCacheKey(
    const std::string& path,
    MaterialTextureColorSpace colorSpace) {
    auto normalized =
        std::filesystem::path(path).lexically_normal().generic_string();
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    normalized += isSRGB(colorSpace) ? "|srgb" : "|linear";
    return normalized;
}

std::string SceneGPUResources::defaultMaterialTextureCacheKey(
    const MaterialTextureRequirement& requirement) {
    return requirement.defaultTexture +
        (isSRGB(requirement.colorSpace) ? "|srgb|" : "|linear|") +
        std::to_string(requirement.defaultColor.x) + "|" +
        std::to_string(requirement.defaultColor.y) + "|" +
        std::to_string(requirement.defaultColor.z) + "|" +
        std::to_string(requirement.defaultColor.w);
}

void SceneGPUResources::uploadBuffer(
    Device& device,
    FrameScheduler& scheduler,
    CommandList& commandList,
    Buffer& destination,
    const void* data,
    uint64_t size) {
    auto staging = device.createStagingBuffer(size);
    if (!staging) {
        return;
    }
    staging->setData(data, size);
    scheduler.executeImmediate(
        commandList,
        [&](CommandList& uploadCommands) {
            uploadCommands.copyBuffer(*staging, destination, size);
        });
}

} // namespace Tasrovy::Renderer
