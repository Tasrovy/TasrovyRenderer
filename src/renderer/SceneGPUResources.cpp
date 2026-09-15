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
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
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
    Format format,
    bool generateMipmaps) {
    const auto source = Tasrovy::Assets::RenderAssetFactory::decodeTexture(path);
    ImageUploadDesc upload{};
    upload.debugName = path;
    upload.width = source.width;
    upload.height = source.height;
    upload.channels = 4;
    upload.format = format;
    upload.generateMipmaps = generateMipmaps;
    upload.pixels = source.pixels;
    return upload;
}

uint32_t readLittleEndianU32(
    const std::array<uint8_t, 148>& header,
    size_t offset) {
    return static_cast<uint32_t>(header[offset]) |
        (static_cast<uint32_t>(header[offset + 1]) << 8u) |
        (static_cast<uint32_t>(header[offset + 2]) << 16u) |
        (static_cast<uint32_t>(header[offset + 3]) << 24u);
}

ImageUploadDesc loadRgba16FloatDds(const std::string& path) {
    constexpr uint32_t DdsHeaderSize = 124u;
    constexpr uint32_t DdsPixelFormatSize = 32u;
    constexpr uint32_t DxgiFormatR16G16B16A16Float = 10u;
    constexpr uint32_t D3d10ResourceDimensionTexture2D = 3u;
    constexpr size_t DdsDx10PayloadOffset = 148u;

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open DDS texture: " + path);
    }

    std::array<uint8_t, DdsDx10PayloadOffset> header{};
    stream.read(
        reinterpret_cast<char*>(header.data()),
        static_cast<std::streamsize>(header.size()));
    if (stream.gcount() != static_cast<std::streamsize>(header.size())) {
        throw std::runtime_error("DDS header is truncated: " + path);
    }

    const bool validMagic =
        header[0] == 'D' && header[1] == 'D' &&
        header[2] == 'S' && header[3] == ' ';
    const bool hasDx10Header =
        header[84] == 'D' && header[85] == 'X' &&
        header[86] == '1' && header[87] == '0';
    if (!validMagic ||
        readLittleEndianU32(header, 4) != DdsHeaderSize ||
        readLittleEndianU32(header, 76) != DdsPixelFormatSize ||
        !hasDx10Header) {
        throw std::runtime_error(
            "Color grading LUT must be a DDS file with a DX10 header: " +
            path);
    }

    const uint32_t width = readLittleEndianU32(header, 16);
    const uint32_t height = readLittleEndianU32(header, 12);
    const uint32_t mipCount = std::max(readLittleEndianU32(header, 28), 1u);
    const uint32_t dxgiFormat = readLittleEndianU32(header, 128);
    const uint32_t resourceDimension = readLittleEndianU32(header, 132);
    const uint32_t arraySize = readLittleEndianU32(header, 140);
    if (dxgiFormat != DxgiFormatR16G16B16A16Float ||
        resourceDimension != D3d10ResourceDimensionTexture2D ||
        arraySize != 1u || mipCount != 1u) {
        throw std::runtime_error(
            "Color grading DDS must be a single-mip, single-layer "
            "R16G16B16A16_FLOAT Texture2D: " + path);
    }

    const uint64_t expectedWidth =
        static_cast<uint64_t>(height) * height;
    const uint64_t expectedPayloadSize =
        static_cast<uint64_t>(width) * height * 8u;
    if (height < 2u || width != expectedWidth) {
        throw std::runtime_error(
            "Flattened color grading LUT must have dimensions N*N by N: " +
            path);
    }

    std::vector<uint8_t> pixels{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    if (pixels.size() != expectedPayloadSize) {
        throw std::runtime_error(
            "Unexpected RGBA16F DDS payload size for color grading LUT: " +
            path);
    }

    ImageUploadDesc upload{};
    upload.debugName = path;
    upload.width = width;
    upload.height = height;
    upload.channels = 4u;
    upload.format = Format::RGBA16Float;
    upload.generateMipmaps = false;
    upload.pixels = std::move(pixels);
    return upload;
}

ImageUploadDesc loadCubemapUpload(
    const std::string& directory,
    Format format = Format::RGBA8Srgb) {
    const auto source =
        Tasrovy::Assets::RenderAssetFactory::decodeCubemap(directory);
    ImageUploadDesc upload{};
    upload.debugName = directory;
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
        const std::string meshName = object->getName().empty()
            ? "UnnamedMesh"
            : object->getName();
        resources.vertexBuffer = device.retainResource(
            sceneScope, device.createVertexBuffer(
                vertexSize, "Mesh." + meshName + ".Vertex"));
        resources.indexBuffer = device.retainResource(
            sceneScope, device.createIndexBuffer(
                indexSize, "Mesh." + meshName + ".Index"));
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
                    ? Format::RGBA8Srgb
                    : Format::RGBA8Unorm)));
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
            binding->path,
            requirement.colorSpace,
            binding->generateMipmaps);
        if (materialTextures_.contains(cacheKey)) {
            continue;
        }
        materialTextures_[cacheKey] = device.retainResource(
            sceneScope,
            device.createTexture(loadTextureUpload(
                binding->path,
                isSRGB(requirement.colorSpace)
                    ? Format::RGBA8Srgb
                    : Format::RGBA8Unorm,
                binding->generateMipmaps)));
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

    const auto* binding = material->resolveTexture(requirement);
    if (!binding || binding->path.empty()) {
        return {fallbackKey, fallbackImage};
    }
    const auto key = materialTextureCacheKey(
        binding->path,
        requirement.colorSpace,
        binding->generateMipmaps);
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
        sceneScope, device.createVertexBuffer(
            vertexSize, "Skybox.Vertex"));
    skyboxIndexBuffer_ = device.retainResource(
        sceneScope, device.createIndexBuffer(
            indexSize, "Skybox.Index"));
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

void SceneGPUResources::prepareGlobalTextures(
    Device& device,
    Device::ResourceScope persistentScope) {
    if (colorGradingLut_) return;

    constexpr const char* ColorGradingLutPath =
        "res/Textures/ColorGrading/LUT.dds";
    auto upload = loadRgba16FloatDds(ColorGradingLutPath);
    colorGradingLut_ = device.retainResource(
        persistentScope,
        device.createTexture(upload));
    LOG_INFO(
        "SceneGPUResources: loaded {}x{} RGBA16F flattened color grading LUT '{}'",
        upload.width,
        upload.height,
        ColorGradingLutPath);
}

void SceneGPUResources::prepareEnvironmentFallbacks(
    Device& device,
    Device::ResourceScope persistentScope) {
    if (!iblFallbackCubemap_) {
        ImageUploadDesc neutralCube;
        neutralCube.debugName = "IBL.FallbackCubemap";
        neutralCube.width = 1;
        neutralCube.height = 1;
        neutralCube.channels = 4;
        neutralCube.arrayLayers = 6;
        neutralCube.format = Format::RGBA8Unorm;
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
                Format::RGBA8Unorm));
    }
}

void SceneGPUResources::prepareSkyboxVariants(
    Device& device,
    Device::ResourceScope persistentScope,
    const std::string& preferredPath) {
    prepareEnvironmentFallbacks(device, persistentScope);

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

const std::shared_ptr<Image>& SceneGPUResources::colorGradingLut() const {
    return colorGradingLut_;
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
    MaterialTextureColorSpace colorSpace,
    bool generateMipmaps) {
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
    normalized += generateMipmaps ? "|mips" : "|no-mips";
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
    auto staging = device.createStagingBuffer(size, "SceneUpload.Staging");
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
