#include "FrameRuntimeParameterCompiler.h"

#include "FrameParameterBuilder.h"
#include "SceneRendererComponents.h"
#include "ViewSystem.h"
#include "../RHI/Buffer.h"
#include "../RHI/CompiledRenderPipeline.h"
#include "../render/Camera.h"
#include "../render/FrameCompiler.h"
#include "../render/FramePacket.h"
#include "../render/Material.h"
#include "../render/Mesh.h"
#include "../render/Object.h"
#include "../render/Scene.h"
#include "Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Tasrovy::Renderer {
namespace {

using namespace Tasrovy::Base;
using namespace Tasrovy::Render;
using namespace Tasrovy::RHI;
using UniformBufferObject = FrameUniformBuffer;
using SkyUniformBufferObject = SkyFrameUniformBuffer;
using PassResources = CompiledPassResources;

struct MaterialPbrValues {
    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
};

MaterialPbrValues resolveMaterialPbr(
    const std::shared_ptr<Material>& material) {
    if (!material) return {};
    return {
        std::clamp(material->getFloat("metallic", 0.0f), 0.0f, 1.0f),
        std::clamp(material->getFloat("roughness", 1.0f), 0.04f, 1.0f),
        std::clamp(material->getFloat("ao", 1.0f), 0.0f, 1.0f)
    };
}

template <typename T>
void storePacketBytes(
    std::vector<std::byte>& destination,
    const T& value) {
    destination.resize(sizeof(T));
    std::memcpy(destination.data(), &value, sizeof(T));
}

bool passUsesFullscreenDraw(const FramePassPacket& pass) {
    return pass.getExecution() == PipelinePassExecution::Fullscreen;
}

bool passUsesSkyboxDraw(const FramePassPacket& pass) {
    return pass.getExecution() == PipelinePassExecution::Skybox;
}

int selectMaterialUvMode(
    const std::string& materialName,
    int bodyMode,
    int hairMode,
    int faceMode) {
    auto lowerName = materialName;
    std::transform(
        lowerName.begin(), lowerName.end(), lowerName.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    if (lowerName.find("hair") != std::string::npos) return hairMode;
    if (lowerName.find("face") != std::string::npos ||
        lowerName.find("eye") != std::string::npos) return faceMode;
    return bodyMode;
}

std::unordered_map<std::string, FrameParameterProvider>& providers() {
    static std::unordered_map<std::string, FrameParameterProvider> value;
    return value;
}

std::mutex& providerMutex() {
    static std::mutex value;
    return value;
}

void ensureBuiltinProviders() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto noOp = [](FrameParameterProviderContext&) {};
        providers().emplace(ParameterProviders::Standard, noOp);
        providers().emplace(ParameterProviders::Shadow, noOp);
        providers().emplace(ParameterProviders::Lighting, noOp);
        providers().emplace(ParameterProviders::Skybox, noOp);
        providers().emplace(
            ParameterProviders::FinalComposite,
            [](FrameParameterProviderContext& context) {
                auto& uniform = context.uniform;
                const auto& settings = context.settings;
                const bool debug = !settings.debugOutputResource.empty();
                uniform.roughnessAo.z = debug
                    ? static_cast<float>(settings.debugOutputSemantic)
                    : 0.0f;
                uniform.lightDir = TSVec4f(settings.outlineColor, 1.0f);
                uniform.lightColor = TSVec4f(
                    settings.outlineThreshold,
                    settings.outlineThickness,
                    settings.outlineStrength,
                    settings.outlineSoftness);
                uniform.ssaoParams = TSVec4f(
                    context.camera.getNearPlane(),
                    context.camera.getFarPlane(),
                    settings.debugDepthRange,
                    settings.debugVelocityScale);
            });
        providers().emplace(
            ParameterProviders::DepthOfField,
            [](FrameParameterProviderContext& context) {
                const auto& settings = context.settings;
                context.uniform.postEffectParams = TSVec4f(
                    settings.dofFocusDistance,
                    settings.dofFocusRange,
                    settings.dofMaxBlurRadius,
                    settings.dofStrength);
            });
        providers().emplace(
            ParameterProviders::MotionBlur,
            [](FrameParameterProviderContext& context) {
                const auto& settings = context.settings;
                context.uniform.postEffectParams = TSVec4f(
                    settings.motionBlurStrength,
                    settings.motionBlurMaxRadius,
                    context.viewFrame.jitterDeltaUv.x,
                    context.viewFrame.jitterDeltaUv.y);
                context.uniform.pcssParams.z =
                    static_cast<float>(settings.motionBlurSamples);
            });
        const auto temporal = [](
            FrameParameterProviderContext& context,
            int requiredMode) {
            const bool history =
                context.settings.temporalAAMode == requiredMode &&
                context.viewState.temporalHistoryValid &&
                context.settings.debugOutputResource.empty();
            context.uniform.taaParams = TSVec4f(
                history ? 1.0f : 0.0f,
                context.settings.taaHistoryWeight,
                static_cast<float>(context.internalWidth) /
                    static_cast<float>(std::max(context.displayWidth, 1u)),
                static_cast<float>(context.internalHeight) /
                    static_cast<float>(std::max(context.displayHeight, 1u)));
        };
        providers().emplace(
            ParameterProviders::TemporalAA,
            [temporal](FrameParameterProviderContext& context) {
                temporal(context, 1);
            });
        providers().emplace(
            ParameterProviders::TemporalUpscale,
            [temporal](FrameParameterProviderContext& context) {
                temporal(context, 2);
            });
        providers().emplace(
            ParameterProviders::OutlineTemporal,
            [](FrameParameterProviderContext& context) {
                const auto& settings = context.settings;
                context.uniform.lightDir =
                    TSVec4f(settings.outlineColor, 1.0f);
                context.uniform.lightColor = TSVec4f(
                    settings.outlineThreshold,
                    settings.outlineThickness,
                    settings.outlineStrength,
                    settings.outlineSoftness);
                context.uniform.taaParams = TSVec4f(
                    settings.outlineTemporalDenoise &&
                            context.viewState.temporalHistoryValid
                        ? 1.0f
                        : 0.0f,
                    settings.outlineHistoryWeight,
                    0.0f,
                    0.0f);
            });
        providers().emplace(
            ParameterProviders::BloomPrefilter,
            [](FrameParameterProviderContext& context) {
                context.uniform.pcssParams.w = 1.0f;
            });
    });
}

bool applyProvider(FrameParameterProviderContext& context) {
    ensureBuiltinProviders();
    FrameParameterProvider provider;
    {
        std::scoped_lock lock(providerMutex());
        const auto found = providers().find(context.pass.parameterProvider);
        if (found == providers().end()) return false;
        provider = found->second;
    }
    provider(context);
    return true;
}

} // namespace

void FrameRuntimeParameterCompiler::registerProvider(
    std::string providerId,
    FrameParameterProvider provider) {
    if (providerId.empty() || !provider) return;
    ensureBuiltinProviders();
    std::scoped_lock lock(providerMutex());
    providers()[std::move(providerId)] = std::move(provider);
}

bool FrameRuntimeParameterCompiler::hasProvider(
    const std::string& providerId) {
    ensureBuiltinProviders();
    std::scoped_lock lock(providerMutex());
    return providers().contains(providerId);
}

void FrameRuntimeParameterCompiler::populate(
    SceneRendererComponents& state,
    Scene& scene,
    Camera& camera,
    const ViewFrameData& viewFrame,
    const std::vector<CompiledPassResources*>& scheduledPasses,
    uint32_t frameIdx,
    uint32_t displayWidth,
    uint32_t displayHeight,
    std::unordered_map<const Object*, TSMat4f>& currentModelMatrices) {
    auto* cam = &camera;
    const TSMat4f& viewMat = viewFrame.view;
    const TSMat4f& unflippedProjMat = viewFrame.unflippedProjection;
    const TSMat4f& projMat = viewFrame.flippedProjection;
    const auto resolutionParameters = FrameParameterBuilder::buildResolution(
        state.internalRenderWidth,
        state.internalRenderHeight,
        displayWidth,
        displayHeight,
        state.settings.temporalAAMode);
    const float temporalMipBias = resolutionParameters.temporalMipBias;

    const FrameLightingParameters lighting =
        FrameParameterBuilder::buildLighting(scene);
    const TSVec3f& lightDir = lighting.primaryDirection;
    const TSVec3f& lightColor = lighting.primaryColor;
    const float lightIntensity = lighting.primaryIntensity;
    const ShadowViewData shadowCascades = state.shadowViewSystem.build(
        *cam,
        lighting.shadowDirection,
        state.settings.csmMaximumDistance,
        state.settings.csmSplitLambda,
        state.settings.shadowTechnique != static_cast<int>(ShadowTechnique::ShadowMap));

    const auto populateExtendedMaterialAndLights =
        [&](UniformBufferObject& ubo, const std::shared_ptr<Material>& material) {
            FrameParameterBuilder::populateMaterialAndLighting(
                ubo,
                material,
                lighting,
                shadowCascades,
                state.settings,
                state.environmentLightingEnabled);
        };

    const auto invokeProvider = [&](
        FramePassPacket& packet,
        UniformBufferObject& uniform,
        std::vector<std::byte>& output) {
        FrameParameterProviderContext context{
            packet,
            uniform,
            output,
            state.settings,
            state.viewState,
            viewFrame,
            *cam,
            state.internalRenderWidth,
            state.internalRenderHeight,
            displayWidth,
            displayHeight
        };
        if (!applyProvider(context)) {
            throw std::invalid_argument(
                "Frame parameter provider '" + packet.parameterProvider +
                "' is not registered");
        }
        if (!context.outputOverridden) {
            storePacketBytes(output, uniform);
        }
    };

    auto& gpuGBuffer = state.gpuDrivenGBuffer.resources();
    if (state.settings.gpuDrivenGBufferEnabled &&
        gpuGBuffer.ready &&
        frameIdx < gpuGBuffer.frameBuffers.size()) {
        GpuDrivenFrameData frameData{};
        frameData.view = transpose(viewMat);
        frameData.proj = transpose(projMat);
        frameData.previousView = transpose(
            state.viewState.temporalHistoryValid ? state.viewState.previousView : viewMat);
        frameData.previousProj = transpose(
            state.viewState.temporalHistoryValid
                ? state.viewState.previousFlippedProjection
                : projMat);
        frameData.uvTransform = TSVec4f(
            state.settings.uvScale[0], state.settings.uvScale[1],
            state.settings.uvOffset[0], state.settings.uvOffset[1]);
        frameData.taaParams = TSVec4f(
            0.0f, 0.0f, temporalMipBias, 0.0f);
        frameData.drawCount =
            static_cast<uint32_t>(gpuGBuffer.draws.size());
        gpuGBuffer.frameBuffers[frameIdx]->setData(
            &frameData, sizeof(frameData));

        std::vector<GpuDrivenDrawData> gpuDrawData;
        gpuDrawData.resize(gpuGBuffer.draws.size());
        for (size_t index = 0; index < gpuGBuffer.draws.size(); ++index) {
            const auto& source = gpuGBuffer.draws[index];
            auto& destination = gpuDrawData[index];
            if (!source.object) {
                continue;
            }
            const auto material = source.material
                ? source.material
                : source.object->getMaterial();
            const auto pbr = resolveMaterialPbr(material);
            const TSMat4f currentModel = source.object->getModelMatrix();
            currentModelMatrices[source.object] = currentModel;
            const auto previousModel = state.viewState.previousModelMatrices.find(source.object);
            const TSMat4f previous = state.viewState.temporalHistoryValid &&
                previousModel != state.viewState.previousModelMatrices.end()
                ? previousModel->second
                : currentModel;
            destination.model = transpose(currentModel);
            destination.previousModel = transpose(previous);
            const TSVec4f baseColor = material
                ? material->getVec4("baseColorFactor", TSVec4f(1.0f))
                : TSVec4f(1.0f);
            destination.baseColorFactorAndTexture = TSVec4f(
                baseColor.x, baseColor.y, baseColor.z,
                material && material->hasTexture("baseColorTexture")
                    ? 1.0f
                    : 0.0f);
            destination.materialParams = TSVec4f(
                pbr.metallic,
                pbr.roughness,
                pbr.ao,
                static_cast<float>(selectMaterialUvMode(
                    source.materialName,
                    state.settings.bodyUvMode,
                    state.settings.hairUvMode,
                    state.settings.faceUvMode)));
            destination.materialEmission = TSVec4f(
                material
                    ? material->getFloat("emissiveIntensity", 0.0f)
                    : 0.0f,
                0.0f, 0.0f, 0.0f);
            const TSVec3f rimColor = material
                ? material->getVec3("rimColor", TSVec3f(1.0f))
                : TSVec3f(1.0f);
            destination.rimColorAndStrength = TSVec4f(
                rimColor,
                material ? material->getFloat("rimStrength", 0.0f) : 0.0f);
            destination.rimParams = TSVec4f(
                material ? material->getFloat("rimPower", 3.0f) : 3.0f,
                0.0f, 0.0f, 0.0f);

            const TSVec4f localCenter(
                source.localBoundsCenter.x,
                source.localBoundsCenter.y,
                source.localBoundsCenter.z,
                1.0f);
            const auto worldCenter = currentModel * localCenter;
            const TSVec3f objectScale = source.object->getScale();
            const float maximumScale = std::max({
                std::abs(objectScale.x),
                std::abs(objectScale.y),
                std::abs(objectScale.z)
            });
            destination.worldBounds = TSVec4f(
                worldCenter.x,
                worldCenter.y,
                worldCenter.z,
                source.localBoundsRadius * maximumScale);
            destination.indexCount = source.indexCount;
            destination.firstIndex = source.firstIndex;
            destination.vertexOffset = source.vertexOffset;
            destination.firstInstance = static_cast<uint32_t>(index);
        }
        gpuGBuffer.drawBuffers[frameIdx]->setData(
            gpuDrawData.data(),
            gpuDrawData.size() * sizeof(GpuDrivenDrawData));

    }

    for (PassResources* scheduledPass : scheduledPasses) {
        auto& pass = *scheduledPass;
        if (!pass.framePass ||
            pass.framePass->parameters.uniformByteSize == 0) {
            continue;
        }

        if (passUsesSkyboxDraw(*pass.framePass)) {
            TSMat4f skyView = TSMat4f(TSMat3f(viewMat));
            SkyUniformBufferObject skyUbo{};
            skyUbo.view = transpose(skyView);
            skyUbo.proj = transpose(projMat);
            storePacketBytes(
                pass.framePass->parameters.uniformData,
                skyUbo);
            UniformBufferObject providerUniform{};
            FrameParameterProviderContext context{
                *pass.framePass,
                providerUniform,
                pass.framePass->parameters.uniformData,
                state.settings,
                state.viewState,
                viewFrame,
                *cam,
                state.internalRenderWidth,
                state.internalRenderHeight,
                displayWidth,
                displayHeight
            };
            if (!applyProvider(context)) {
                throw std::invalid_argument(
                    "Frame parameter provider '" +
                    pass.framePass->parameterProvider +
                    "' is not registered");
            }
        } else if (passUsesFullscreenDraw(*pass.framePass)) {
            UniformBufferObject ubo{};
            ubo.model = transpose(TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), 1.0f);
            ubo.roughnessAo = TSVec4f(
                1.0f,
                1.0f,
                0.0f,
                state.settings.bloomRadius);
            const bool isPostProcessingPass =
                pass.framePass->getType() == PipelinePassType::PostProcess;
            ubo.uvTransform = isPostProcessingPass
                ? TSVec4f(
                    state.settings.bloomEnabled ? 1.0f : 0.0f,
                    state.settings.bloomThreshold,
                    state.settings.bloomIntensity,
                    state.settings.exposure)
                : TSVec4f(
                    state.settings.uvScale[0], state.settings.uvScale[1],
                    state.settings.uvOffset[0], state.settings.uvOffset[1]);
            populateExtendedMaterialAndLights(ubo, nullptr);
            ubo.previousView = transpose(
                state.viewState.temporalHistoryValid ? state.viewState.previousView : viewMat);
            ubo.previousProj = transpose(
                state.viewState.temporalHistoryValid ? state.viewState.previousFlippedProjection : projMat);
            ubo.previousModel = transpose(TSMat4f(1.0f));
            ubo.taaParams = TSVec4f(
                0.0f,
                state.settings.taaHistoryWeight,
                static_cast<float>(state.internalRenderWidth) /
                    static_cast<float>(std::max(displayWidth, 1u)),
                static_cast<float>(state.internalRenderHeight) /
                    static_cast<float>(std::max(displayHeight, 1u)));
            invokeProvider(
                *pass.framePass,
                ubo,
                pass.framePass->parameters.uniformData);
        } else if (pass.framePass->getExecution() == PipelinePassExecution::Mesh) {
            // Mesh pass descriptors contain per-draw model/material state and are updated when each submesh is drawn.
        } else {
            UniformBufferObject ubo{};
            ubo.model = transpose(TSMat4f(1.0f));
            ubo.view = transpose(viewMat);
            ubo.proj = transpose(projMat);
            ubo.previousView = transpose(
                state.viewState.temporalHistoryValid
                    ? state.viewState.previousView
                    : viewMat);
            ubo.previousProj = transpose(
                state.viewState.temporalHistoryValid
                    ? state.viewState.previousFlippedProjection
                    : projMat);
            populateExtendedMaterialAndLights(ubo, nullptr);
            invokeProvider(
                *pass.framePass,
                ubo,
                pass.framePass->parameters.uniformData);
        }
    }

    const auto updateMeshDrawDescriptors =
        [&](PassResources& pass,
            const Object& object,
            const std::shared_ptr<Material>& submeshMaterial,
            uint32_t submeshIndex,
            const std::string& submeshMaterialName,
            uint32_t descriptorSlot) -> bool {
            if (!pass.framePass ||
                descriptorSlot >= pass.framePass->draws.size()) {
                LOG_WARN(
                    "SceneRenderer: pass '{}' descriptor slot {} out of {}",
                    pass.framePass ? pass.framePass->getName() : std::string("<null>"),
                    descriptorSlot,
                    pass.framePass
                        ? pass.framePass->draws.size()
                        : 0u);
                return false;
            }

            const auto material = submeshMaterial ? submeshMaterial : object.getMaterial();
            const auto pbr = resolveMaterialPbr(material);

            UniformBufferObject ubo{};
            const TSMat4f currentModel = object.getModelMatrix();
            if (pass.framePass->getType() == PipelinePassType::Geometry) {
                currentModelMatrices[&object] = currentModel;
            }
            ubo.model = transpose(currentModel);
            const bool isShadowPass =
                pass.framePass->getType() == PipelinePassType::Shadow;
            const size_t cascadeIndex = isShadowPass
                ? std::min<size_t>(pass.framePass->viewIndex, 3u)
                : 0;
            ubo.view = transpose(
                isShadowPass
                    ? shadowCascades.views[cascadeIndex]
                    : viewMat);
            ubo.proj = transpose(
                isShadowPass
                    ? shadowCascades.projections[cascadeIndex]
                    : (object.getFlipProjectionY() ? projMat : unflippedProjMat));
            ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
            ubo.lightColor = TSVec4f(lightColor, lightIntensity);
            ubo.camPosAndMetallic = TSVec4f(cam->getPosition(), pbr.metallic);
            const int uvMode = selectMaterialUvMode(
                submeshMaterialName,
                state.settings.bodyUvMode,
                state.settings.hairUvMode,
                state.settings.faceUvMode);
            ubo.roughnessAo = TSVec4f(
                pbr.roughness,
                pbr.ao,
                0.0f,
                static_cast<float>(uvMode));
            ubo.uvTransform = TSVec4f(
                state.settings.uvScale[0], state.settings.uvScale[1],
                state.settings.uvOffset[0], state.settings.uvOffset[1]);
            populateExtendedMaterialAndLights(ubo, material);
            ubo.previousView = transpose(
                state.viewState.temporalHistoryValid ? state.viewState.previousView : viewMat);
            const TSMat4f& previousObjectProjection = object.getFlipProjectionY()
                ? state.viewState.previousFlippedProjection
                : state.viewState.previousUnflippedProjection;
            ubo.previousProj = transpose(
                state.viewState.temporalHistoryValid
                    ? previousObjectProjection
                    : (object.getFlipProjectionY() ? projMat : unflippedProjMat));
            const auto previousModel = state.viewState.previousModelMatrices.find(&object);
            ubo.previousModel = transpose(
                state.viewState.temporalHistoryValid && previousModel != state.viewState.previousModelMatrices.end()
                    ? previousModel->second
                    : currentModel);
            // Low-resolution rasterization increases texture derivatives and
            // would otherwise select blurrier mips before TAAU can accumulate
            // their detail. The GBuffer shader reads the bias from z.
            ubo.taaParams = TSVec4f(0.0f, 0.0f, temporalMipBias, 0.0f);
            invokeProvider(
                *pass.framePass,
                ubo,
                pass.framePass->draws[descriptorSlot].uniformData);
            if (!state.loggedSubmeshMaterialBindings) {
                LOG_INFO(
                    "SceneRenderer: pass '{}' submesh {} '{}' baseColor '{}'",
                    pass.framePass ? pass.framePass->getName() : std::string("<null>"),
                    submeshIndex,
                    submeshMaterialName,
                    material ? material->getTexture("baseColorTexture") : std::string("<null>"));
            }
            return true;
        };

    // Runtime parameter generation only fills FramePacket data. Concrete
    // descriptors, pipelines, barriers and commands are consumed by the RHI
    // executor below.
    const auto& sources = state.frameOrchestrator.sourceRegistry();
    for (PassResources* scheduledPass : scheduledPasses) {
        auto& pass = *scheduledPass;
        if (!pass.framePass ||
            pass.framePass->getExecution() != PipelinePassExecution::Mesh) {
            continue;
        }
        for (uint32_t drawIndex = 0;
             drawIndex < static_cast<uint32_t>(pass.framePass->draws.size());
             ++drawIndex) {
            const auto& drawPacket = pass.framePass->draws[drawIndex];
            const auto foundObject = sources.objects.find(drawPacket.objectId);
            const auto object = foundObject == sources.objects.end()
                ? nullptr
                : foundObject->second.lock();
            if (!object) {
                continue;
            }
            const auto foundMaterial =
                sources.materials.find(drawPacket.materialId);
            const auto material = foundMaterial == sources.materials.end()
                ? object->getMaterial()
                : foundMaterial->second;
            std::string materialName = "<mesh>";
            if (const auto mesh = object->getMesh();
                mesh && drawPacket.submeshIndex < mesh->getSubmeshes().size()) {
                materialName =
                    mesh->getSubmeshes()[drawPacket.submeshIndex]
                        .getMaterialName();
            }
            (void)updateMeshDrawDescriptors(
                pass, *object, material, drawPacket.submeshIndex,
                materialName, drawIndex);
        }
    }


}

} // namespace Tasrovy::Renderer
