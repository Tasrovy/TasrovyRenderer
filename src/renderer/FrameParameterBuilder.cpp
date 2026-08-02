#include "FrameParameterBuilder.h"

#include "../render/Light.h"
#include "../render/Material.h"
#include "../render/Scene.h"

#include <algorithm>
#include <cmath>

namespace Tasrovy::Renderer {

using namespace Tasrovy::Render;
using namespace Tasrovy::Base;

FrameLightingParameters FrameParameterBuilder::buildLighting(
    const Scene& scene) {
    FrameLightingParameters result{};
    if (!scene.getLights().empty() && scene.getLights().front()) {
        const auto& light = *scene.getLights().front();
        result.primaryDirection = light.getDirection();
        result.primaryColor = light.getColor();
        result.primaryIntensity = light.getIntensity();
    }

    for (const auto& light : scene.getLights()) {
        if (light && dynamic_cast<const AreaLight*>(light.get())) {
            result.shadowLight = light.get();
            break;
        }
    }
    if (!result.shadowLight) {
        for (const auto& light : scene.getLights()) {
            if (light && dynamic_cast<const DirectionalLight*>(light.get())) {
                result.shadowLight = light.get();
                break;
            }
        }
    }

    for (const auto& light : scene.getLights()) {
        if (!light || result.gpuLightCount >= MaxSceneLights) {
            continue;
        }
        auto& gpuLight = result.gpuLights[result.gpuLightCount];
        gpuLight.colorAndIntensity =
            TSVec4f(light->getColor(), light->getIntensity());
        if (const auto* directional =
                dynamic_cast<const DirectionalLight*>(light.get())) {
            gpuLight.positionAndType = TSVec4f(0.0f, 0.0f, 0.0f, 0.0f);
            gpuLight.directionAndRange =
                TSVec4f(normalize(directional->getDirection()), 0.0f);
        } else if (const auto* point =
                       dynamic_cast<const PointLight*>(light.get())) {
            gpuLight.positionAndType = TSVec4f(point->getPosition(), 1.0f);
            gpuLight.parameters = TSVec4f(
                point->getConstant(), point->getLinear(),
                point->getQuadratic(), 0.0f);
        } else if (const auto* area =
                       dynamic_cast<const AreaLight*>(light.get())) {
            gpuLight.positionAndType = TSVec4f(area->getPosition(), 2.0f);
            gpuLight.directionAndRange =
                TSVec4f(normalize(area->getDirection()), 0.0f);
            gpuLight.parameters = TSVec4f(
                area->getWidth(), area->getHeight(),
                area->isTwoSided() ? 1.0f : 0.0f, 0.0f);
        } else if (const auto* spot =
                       dynamic_cast<const SpotLight*>(light.get())) {
            gpuLight.positionAndType = TSVec4f(spot->getPosition(), 1.0f);
            gpuLight.directionAndRange = TSVec4f(
                normalize(spot->getDirection()), spot->getCutoff());
            gpuLight.parameters = TSVec4f(1.0f, 0.09f, 0.032f, 0.0f);
        } else {
            continue;
        }
        if (light.get() == result.shadowLight) {
            result.shadowLightIndex =
                static_cast<int32_t>(result.gpuLightCount);
        }
        ++result.gpuLightCount;
    }

    result.shadowDirection = normalize(result.primaryDirection);
    if (const auto* directional =
            dynamic_cast<const DirectionalLight*>(result.shadowLight)) {
        result.shadowDirection = normalize(directional->getDirection());
    } else if (const auto* area =
                   dynamic_cast<const AreaLight*>(result.shadowLight)) {
        result.shadowDirection = normalize(area->getDirection());
    }
    return result;
}

FrameResolutionParameters FrameParameterBuilder::buildResolution(
    uint32_t internalWidth,
    uint32_t internalHeight,
    uint32_t displayWidth,
    uint32_t displayHeight,
    int temporalMode) {
    FrameResolutionParameters result{};
    result.internalToDisplayScale = std::min(
        static_cast<float>(internalWidth) /
            static_cast<float>(std::max(displayWidth, 1u)),
        static_cast<float>(internalHeight) /
            static_cast<float>(std::max(displayHeight, 1u)));
    result.temporalMipBias = temporalMode == 2
        ? std::clamp(
              std::log2(std::max(result.internalToDisplayScale, 0.25f)),
              -2.0f,
              0.0f)
        : 0.0f;
    return result;
}

void FrameParameterBuilder::populateMaterialAndLighting(
    FrameUniformBuffer& uniform,
    const std::shared_ptr<Material>& material,
    const FrameLightingParameters& lighting,
    const ShadowViewData& shadowViews,
    const RendererSettings& settings,
    bool environmentLightingEnabled) {
    const TSVec4f baseColorFactor = material
        ? material->getVec4("baseColorFactor", TSVec4f(1.0f))
        : TSVec4f(1.0f);
    uniform.baseColorFactorAndTexture = TSVec4f(
        baseColorFactor.x,
        baseColorFactor.y,
        baseColorFactor.z,
        material && !material->getTexture("baseColorTexture").empty()
            ? 1.0f
            : 0.0f);
    uniform.materialEmission = TSVec4f(
        material ? material->getFloat("emissiveIntensity", 0.0f) : 0.0f,
        0.0f, 0.0f, 0.0f);
    const TSVec3f rimColor = material
        ? material->getVec3("rimColor", TSVec3f(1.0f))
        : TSVec3f(1.0f);
    uniform.materialRimColorAndStrength = TSVec4f(
        rimColor,
        material ? material->getFloat("rimStrength", 0.0f) : 0.0f);
    uniform.materialRimParams = TSVec4f(
        material ? material->getFloat("rimPower", 3.0f) : 3.0f,
        0.0f, 0.0f, 0.0f);
    uniform.lightMeta = TSVec4f(
        static_cast<float>(lighting.gpuLightCount),
        environmentLightingEnabled ? 1.0f : 0.0f,
        0.0f,
        0.0f);
    uniform.lights = lighting.gpuLights;
    uniform.lightViewProj = transpose(shadowViews.viewProjections[0]);
    uniform.shadowParams = TSVec4f(
        settings.shadowSlopeBias,
        settings.shadowMinimumBias,
        static_cast<float>(lighting.shadowLightIndex),
        settings.shadowStrength);
    uniform.advancedLightingParams = TSVec4f(
        settings.pcssEnabled ? 1.0f : 0.0f,
        settings.ssaoEnabled ? 1.0f : 0.0f,
        0.0f,
        settings.ssrEnabled ? 1.0f : 0.0f);
    uniform.pcssParams = TSVec4f(
        settings.pcssLightSize, settings.pcssMaxFilterRadius, 0.0f, 0.0f);
    uniform.ssaoParams = TSVec4f(
        settings.ssaoRadiusPixels,
        settings.ssaoIntensity,
        settings.ssaoWorldRadius,
        settings.ssaoBias);
    uniform.postEffectParams = TSVec4f(0.0f);
    uniform.ssrParams = TSVec4f(
        settings.ssrMaxDistance,
        settings.ssrStepSize,
        settings.ssrThickness,
        settings.ssrIntensity);
    for (size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade) {
        uniform.csmLightViewProj[cascade] =
            transpose(shadowViews.viewProjections[cascade]);
    }
    uniform.csmSplits = TSVec4f(
        shadowViews.splits[0],
        shadowViews.splits[1],
        shadowViews.splits[2],
        shadowViews.splits[3]);
    const bool cascadesEnabled =
        settings.shadowTechnique != static_cast<int>(ShadowTechnique::ShadowMap);
    uniform.csmParams = TSVec4f(
        cascadesEnabled ? static_cast<float>(ShadowCascadeCount) : 1.0f,
        cascadesEnabled ? settings.csmBlendFraction : 0.0f,
        shadowViews.splits[ShadowCascadeCount - 1],
        static_cast<float>(settings.shadowTechnique));
    uniform.vsmPageTable = shadowViews.virtualPageTable;
    uniform.vsmParams = TSVec4f(
        static_cast<float>(VirtualShadowAtlasResolution),
        static_cast<float>(VirtualShadowPageResolution),
        static_cast<float>(ShadowCascadeCount),
        0.0f);
}

} // namespace Tasrovy::Renderer
