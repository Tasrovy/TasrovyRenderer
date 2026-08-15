#include "FrameRuntimeParameterCompiler.h"

#include "FrameParameterBuilder.h"
#include "SceneRendererComponents.h"
#include "ViewSystem.h"
#include "../RHI/Buffer.h"
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
#include <utility>

namespace Tasrovy::Renderer {
namespace {

using namespace Tasrovy::Base;
using namespace Tasrovy::Render;
using namespace Tasrovy::RHI;
using UniformBufferObject = FrameUniformBuffer;
using SkyUniformBufferObject = SkyFrameUniformBuffer;
using PassResources = FramePassPacket;

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
        providers().emplace(
            ParameterProviders::Shadow,
            [](FrameParameterProviderContext& context) {
                ShadowPassConstants constants{
                    context.uniform.model,
                    context.uniform.view,
                    context.uniform.proj};
                storePacketBytes(context.output, constants);
                context.outputOverridden = true;
            });
        providers().emplace(
            ParameterProviders::Lighting,
            [](FrameParameterProviderContext& context) {
                const auto& source = context.uniform;
                LightingPassConstants constants{};
                constants.lightViewProjection = source.lightViewProj;
                constants.shadowParameters = source.shadowParams;
                constants.featureFlags = source.advancedLightingParams;
                constants.pcssParameters = source.pcssParams;
                constants.cascadeViewProjections = source.csmLightViewProj;
                constants.cascadeSplits = source.csmSplits;
                constants.cascadeParameters = source.csmParams;
                constants.virtualShadowPages = source.vsmPageTable;
                constants.virtualShadowParameters = source.vsmParams;
                storePacketBytes(context.output, constants);
                context.outputOverridden = true;
            });
        providers().emplace(ParameterProviders::Skybox, noOp);
        providers().emplace(
            ParameterProviders::SSAO,
            [](FrameParameterProviderContext& context) {
                SsaoPassConstants constants;
                constants.parameters = TSVec4f(
                    context.settings.ssaoRadiusPixels,
                    context.settings.ssaoIntensity,
                    context.settings.ssaoWorldRadius,
                    context.settings.ssaoBias);
                storePacketBytes(context.output, constants);
                context.outputOverridden = true;
            });
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
            TemporalPassConstants constants;
            constants.parameters = TSVec4f(
                history ? 1.0f : 0.0f,
                context.settings.taaHistoryWeight,
                static_cast<float>(context.internalWidth) /
                    static_cast<float>(std::max(context.displayWidth, 1u)),
                static_cast<float>(context.internalHeight) /
                    static_cast<float>(std::max(context.displayHeight, 1u)));
            storePacketBytes(context.output, constants);
            context.outputOverridden = true;
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
                BloomPassConstants constants;
                constants.parameters = TSVec4f(
                    context.pass.getName() == "BloomDownHalf" ? 1.0f : 0.0f,
                    context.settings.bloomThreshold,
                    context.settings.bloomIntensity,
                    context.settings.bloomRadius);
                storePacketBytes(context.output, constants);
                context.outputOverridden = true;
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
    const std::vector<FramePassPacket*>& scheduledPasses,
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

    for (PassResources* scheduledPass : scheduledPasses) {
        auto& pass = *scheduledPass;
        if (pass.parameters.uniformByteSize == 0) {
            continue;
        }

        if (passUsesSkyboxDraw(pass)) {
            TSMat4f skyView = TSMat4f(TSMat3f(viewMat));
            SkyUniformBufferObject skyUbo{};
            skyUbo.view = transpose(skyView);
            skyUbo.proj = transpose(projMat);
            storePacketBytes(
                pass.parameters.uniformData,
                skyUbo);
            UniformBufferObject providerUniform{};
            FrameParameterProviderContext context{
                pass,
                providerUniform,
                pass.parameters.uniformData,
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
                    pass.parameterProvider +
                    "' is not registered");
            }
        } else if (passUsesFullscreenDraw(pass)) {
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
                pass.getType() == PipelinePassType::PostProcess;
            ubo.uvTransform = isPostProcessingPass
                ? TSVec4f(
                    state.settings.bloomEnabled ? 1.0f : 0.0f,
                    state.settings.bloomThreshold,
                    state.settings.bloomIntensity,
                    state.settings.exposure)
                : TSVec4f(
                    1.0f, 1.0f, 0.0f, 0.0f);
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
                pass,
                ubo,
                pass.parameters.uniformData);
        } else if (pass.getExecution() == PipelinePassExecution::Mesh) {
            // Object and material state live in GPUScene SSBOs. The mesh pass
            // now uploads one pass-constant block per frame, shared by every
            // draw descriptor set in the pass.
            UniformBufferObject ubo{};
            const bool shadowPass =
                pass.getType() == PipelinePassType::Shadow;
            const size_t cascade = shadowPass
                ? std::min<size_t>(pass.viewIndex, 3u) : 0u;
            ubo.model = transpose(TSMat4f(1.0f));
            ubo.view = transpose(shadowPass
                ? shadowCascades.views[cascade] : viewMat);
            ubo.proj = transpose(shadowPass
                ? shadowCascades.projections[cascade] : projMat);
            ubo.previousView = transpose(state.viewState.temporalHistoryValid
                ? state.viewState.previousView : viewMat);
            ubo.previousProj = transpose(state.viewState.temporalHistoryValid
                ? state.viewState.previousFlippedProjection : projMat);
            ubo.previousModel = transpose(TSMat4f(1.0f));
            ubo.taaParams = TSVec4f(0.0f, 0.0f, temporalMipBias, 0.0f);
            populateExtendedMaterialAndLights(ubo, nullptr);
            invokeProvider(
                pass, ubo,
                pass.parameters.uniformData);
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
                pass,
                ubo,
                pass.parameters.uniformData);
        }
    }

    // Temporal history is still committed by ViewSystem, but gathering it is
    // independent from draw-packet construction now.
    const auto& sources = state.frameOrchestrator.sourceRegistry();
    for (const auto& [_, weakObject] : sources.objects) {
        if (const auto object = weakObject.lock()) {
            currentModelMatrices[object.get()] = object->getModelMatrix();
        }
    }


}

} // namespace Tasrovy::Renderer
