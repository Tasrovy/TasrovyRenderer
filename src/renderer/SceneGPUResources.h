#pragma once

#include "../RHI/Device.h"
#include "../render/MaterialTexture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::RHI {
class Buffer;
class CommandList;
class FrameScheduler;
class Image;
}

namespace Tasrovy::Render {
class Material;
class Mesh;
class Object;
}

namespace Tasrovy::Renderer {

struct MeshGPUResources {
    std::shared_ptr<Tasrovy::RHI::Buffer> vertexBuffer;
    std::shared_ptr<Tasrovy::RHI::Buffer> indexBuffer;
    uint32_t indexCount = 0;
};

struct ResolvedMaterialTexture {
    std::string cacheKey;
    std::shared_ptr<Tasrovy::RHI::Image> image;
};

struct SkyboxVariant {
    std::string name;
    std::string path;
    std::shared_ptr<Tasrovy::RHI::Image> cubemap;
};

// Owns scene-lifetime GPU geometry and material texture caches. ResourceScope
// remains the authoritative lifetime owner; this registry supplies stable
// lookup and upload behavior to the renderer runtime.
class SceneGPUResources {
public:
    void resetScene();

    void rebuildMeshes(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::FrameScheduler& scheduler,
        Tasrovy::RHI::CommandList& commandList,
        Tasrovy::RHI::Device::ResourceScope sceneScope,
        const std::vector<std::shared_ptr<Tasrovy::Render::Object>>& objects);

    void ensureDefaultTexture(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::Device::ResourceScope sceneScope,
        const Tasrovy::Render::MaterialTextureRequirement& requirement);

    void ensureMaterialTextures(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::Device::ResourceScope sceneScope,
        const std::shared_ptr<Tasrovy::Render::Material>& material,
        const std::vector<Tasrovy::Render::MaterialTextureRequirement>& requirements);

    ResolvedMaterialTexture resolveMaterialTexture(
        const std::shared_ptr<Tasrovy::Render::Material>& material,
        const Tasrovy::Render::MaterialTextureRequirement& requirement) const;

    const MeshGPUResources* findMesh(
        const Tasrovy::Render::Mesh& mesh) const;

    void rebuildSkyboxGeometry(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::FrameScheduler& scheduler,
        Tasrovy::RHI::CommandList& commandList,
        Tasrovy::RHI::Device::ResourceScope sceneScope,
        bool enabled);

    void prepareSkyboxVariants(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::Device::ResourceScope persistentScope,
        const std::string& preferredPath);
    bool selectSkybox(int index);

    const std::shared_ptr<Tasrovy::RHI::Buffer>& skyboxVertexBuffer() const;
    const std::shared_ptr<Tasrovy::RHI::Buffer>& skyboxIndexBuffer() const;
    const std::shared_ptr<Tasrovy::RHI::Image>& skyCubemap() const;
    const std::shared_ptr<Tasrovy::RHI::Image>& iblFallbackCubemap() const;
    const std::shared_ptr<Tasrovy::RHI::Image>& iblFallbackLut() const;
    const std::vector<SkyboxVariant>& skyboxVariants() const;
    const std::string& activeSkyboxName() const;
    int selectedSkyboxIndex() const;
    uint32_t skyboxIndexCount() const;

    size_t meshCount() const;
    size_t materialTextureCount() const;
    uint64_t meshBufferBytes() const;
    uint64_t skyboxBufferBytes() const;

private:
    static bool isSRGB(
        Tasrovy::Render::MaterialTextureColorSpace colorSpace);
    static std::string materialTextureCacheKey(
        const std::string& path,
        Tasrovy::Render::MaterialTextureColorSpace colorSpace);
    static std::string defaultMaterialTextureCacheKey(
        const Tasrovy::Render::MaterialTextureRequirement& requirement);
    static void uploadBuffer(
        Tasrovy::RHI::Device& device,
        Tasrovy::RHI::FrameScheduler& scheduler,
        Tasrovy::RHI::CommandList& commandList,
        Tasrovy::RHI::Buffer& destination,
        const void* data,
        uint64_t size);

    std::unordered_map<const Tasrovy::Render::Mesh*, MeshGPUResources> meshes_;
    std::unordered_map<std::string, std::shared_ptr<Tasrovy::RHI::Image>>
        materialTextures_;
    std::unordered_map<std::string, std::shared_ptr<Tasrovy::RHI::Image>>
        defaultMaterialTextures_;
    std::shared_ptr<Tasrovy::RHI::Buffer> skyboxVertexBuffer_;
    std::shared_ptr<Tasrovy::RHI::Buffer> skyboxIndexBuffer_;
    uint32_t skyboxIndexCount_ = 0;
    std::vector<SkyboxVariant> skyboxVariants_;
    std::shared_ptr<Tasrovy::RHI::Image> skyCubemap_;
    std::shared_ptr<Tasrovy::RHI::Image> iblFallbackCubemap_;
    std::shared_ptr<Tasrovy::RHI::Image> iblFallbackLut_;
    std::string activeSkyboxName_;
    int selectedSkyboxIndex_ = 0;
};

} // namespace Tasrovy::Renderer
