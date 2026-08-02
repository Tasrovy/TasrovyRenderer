#include "FrameBindingResolver.h"

#include "RendererSettings.h"
#include "SceneGPUResources.h"
#include "ViewState.h"
#include "../RHI/CompiledRenderPipeline.h"
#include "../RHI/Device.h"
#include "../render/FrameCompiler.h"
#include "../render/FramePacket.h"
#include "../render/Material.h"
#include "../render/Mesh.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Tasrovy::Renderer {
namespace {

using Provider = FrameBindingResolver::ImportedResourceProvider;

std::unordered_map<std::string, Provider>& importedProviders() {
    static std::unordered_map<std::string, Provider> value;
    return value;
}

std::mutex& importedProviderMutex() {
    static std::mutex value;
    return value;
}

void ensureBuiltinImportedProviders() {
    using namespace Tasrovy::Render;
    using namespace Tasrovy::RHI;
    static std::once_flag once;
    std::call_once(once, [] {
        importedProviders().emplace(
            ImportedResourceHandles::Skybox,
            [](Device&, SceneGPUResources& resources) {
                FrameImportedImageBinding binding;
                binding.image = resources.skyCubemap();
                return binding;
            });
        const auto iblCube = [](IBLMapType type) {
            return [type](Device& device, SceneGPUResources& resources) {
                FrameImportedImageBinding binding;
                binding.imageInfo = device.getIBLDescriptorInfo(
                    type, resources.activeSkyboxName());
                binding.useImageInfo = binding.imageInfo.nativeView != 0;
                if (!binding.useImageInfo) {
                    binding.image = resources.iblFallbackCubemap();
                }
                return binding;
            };
        };
        importedProviders().emplace(
            ImportedResourceHandles::IblIrradiance,
            iblCube(IBLMapType::Irradiance));
        importedProviders().emplace(
            ImportedResourceHandles::IblPrefiltered,
            iblCube(IBLMapType::Prefiltered));
        importedProviders().emplace(
            ImportedResourceHandles::IblBrdfLut,
            [](Device& device, SceneGPUResources& resources) {
                FrameImportedImageBinding binding;
                binding.imageInfo = device.getIBLDescriptorInfo(
                    IBLMapType::BrdfLut,
                    resources.activeSkyboxName());
                binding.useImageInfo = binding.imageInfo.nativeView != 0;
                if (!binding.useImageInfo) {
                    binding.image = resources.iblFallbackLut();
                }
                return binding;
            });
    });
}

} // namespace

void FrameBindingResolver::registerImportedResource(
    std::string handle,
    ImportedResourceProvider provider) {
    if (handle.empty() || !provider) return;
    ensureBuiltinImportedProviders();
    std::scoped_lock lock(importedProviderMutex());
    importedProviders()[std::move(handle)] = std::move(provider);
}

bool FrameBindingResolver::hasImportedResource(
    const std::string& handle) {
    ensureBuiltinImportedProviders();
    std::scoped_lock lock(importedProviderMutex());
    return importedProviders().contains(handle);
}

Tasrovy::RHI::FrameExecutionBindings FrameBindingResolver::resolve(
    Tasrovy::RHI::Device& device,
    SceneGPUResources& sceneResources,
    const Tasrovy::Render::FrameSourceRegistry& sources,
    const std::vector<Tasrovy::RHI::CompiledPassResources*>& passes,
    const RendererSettings& settings,
    const ViewState& viewState) {
    using namespace Tasrovy::Render;
    using namespace Tasrovy::RHI;

    FrameExecutionBindings bindings;
    for (const auto& [meshId, mesh] : sources.meshes) {
        if (!mesh) continue;
        const auto* gpuMesh = sceneResources.findMesh(*mesh);
        if (!gpuMesh) continue;
        bindings.meshes.emplace(
            meshId,
            FrameMeshBinding{
                gpuMesh->vertexBuffer,
                gpuMesh->indexBuffer,
                gpuMesh->indexCount
            });
    }

    for (const auto& [materialId, material] : sources.materials) {
        if (!material) continue;
        auto& textures = bindings.materialTextures[materialId];
        for (const auto* pass : passes) {
            if (!pass || !pass->framePass) continue;
            for (const auto& requirement :
                 pass->framePass->materialTextures) {
                if (textures.contains(requirement.slot)) continue;
                const auto image = sceneResources.resolveMaterialTexture(
                    material, requirement).image;
                if (image) textures.emplace(requirement.slot, image);
            }
        }
    }

    bindings.skyboxVertexBuffer = sceneResources.skyboxVertexBuffer();
    bindings.skyboxIndexBuffer = sceneResources.skyboxIndexBuffer();
    bindings.skyboxIndexCount = sceneResources.skyboxIndexCount();

    std::unordered_set<std::string> requiredImports;
    for (const auto* pass : passes) {
        if (!pass || !pass->framePass) continue;
        for (const auto& write : pass->framePass->descriptorWrites) {
            if (write.source == FrameDescriptorSource::ImportedResource &&
                !write.importedResource.empty()) {
                requiredImports.insert(write.importedResource);
            }
        }
    }
    ensureBuiltinImportedProviders();
    for (const auto& handle : requiredImports) {
        Provider provider;
        {
            std::scoped_lock lock(importedProviderMutex());
            const auto found = importedProviders().find(handle);
            if (found == importedProviders().end()) {
                throw std::invalid_argument(
                    "Imported resource handle '" + handle +
                    "' has no registered provider");
            }
            provider = found->second;
        }
        bindings.importedImages.emplace(
            handle, provider(device, sceneResources));
    }

    for (auto* pass : passes) {
        if (!pass || !pass->framePass) continue;
        auto& packet = *pass->framePass;
        if (!pass->permutations.empty() &&
            packet.parameterProvider ==
                ParameterProviders::FinalComposite) {
            const bool outlineOnly = settings.debugOutputSemantic ==
                DebugTextureSemantic::OutlineBlackLines;
            packet.selectedPermutationKey =
                (settings.bloomEnabled ? 2u : 0u) |
                (settings.outlineEnabled || outlineOnly ? 4u : 0u);
        }

        for (const auto& write : packet.descriptorWrites) {
            if (write.source != FrameDescriptorSource::RenderTexture) {
                continue;
            }
            std::string resourceName = write.resourceName;
            bool previousFrame = write.previousFrame;
            const auto input = std::find_if(
                packet.sampledTextures.begin(),
                packet.sampledTextures.end(),
                [&](const SampledTextureInput& value) {
                    return value.binding == write.binding;
                });
            const bool debugOverride = packet.parameterProvider ==
                    ParameterProviders::FinalComposite &&
                !settings.debugOutputResource.empty() &&
                settings.debugOutputSemantic !=
                    DebugTextureSemantic::OutlineBlackLines;
            if (debugOverride && write.binding == 1) {
                resourceName = settings.debugOutputResource;
                previousFrame = false;
            } else if (previousFrame &&
                       !viewState.temporalHistoryValid) {
                previousFrame = false;
                if (input != packet.sampledTextures.end()) {
                    if (input->slot == "taaHistoryColor") {
                        resourceName = "SceneColor";
                    } else if (input->slot == "taaHistoryData" ||
                               input->slot == "outlineHistory") {
                        resourceName = "GBufferNormal";
                    }
                }
            }
            if (resourceName != write.resourceName ||
                previousFrame != write.previousFrame) {
                bindings.textureOverrides[packet.id].emplace(
                    write.binding,
                    FrameTextureBindingOverride{
                        std::move(resourceName), previousFrame
                    });
            }
        }
    }
    return bindings;
}

} // namespace Tasrovy::Renderer
