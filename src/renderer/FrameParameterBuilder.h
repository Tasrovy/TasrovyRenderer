#pragma once

#include "RendererSettings.h"
#include "ShadowViewSystem.h"
#include "../base/TSMatrix.h"
#include "../base/TSVector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Tasrovy::Render {
class Light;
class Material;
class Scene;
}

namespace Tasrovy::Renderer {

using Tasrovy::Base::TSVec3f;
using Tasrovy::Base::TSVec4f;
using Tasrovy::Base::TSMat4f;

inline constexpr size_t MaxSceneLights = 256;
// Legacy per-pass uniforms retain their original ABI while all active scene
// lights are uploaded through GPUScene::SceneLightData.
inline constexpr size_t MaxLegacyUniformLights = 8;

struct GpuLightData {
    TSVec4f positionAndType = TSVec4f(0.0f);
    TSVec4f directionAndRange = TSVec4f(0.0f);
    TSVec4f colorAndIntensity = TSVec4f(0.0f);
    TSVec4f parameters = TSVec4f(0.0f);
};

struct FrameLightingParameters {
    TSVec3f primaryDirection = TSVec3f(-0.5f, -1.0f, -0.8f);
    TSVec3f primaryColor = TSVec3f(1.0f);
    float primaryIntensity = 10.0f;
    const Render::Light* shadowLight = nullptr;
    TSVec3f shadowDirection = TSVec3f(-0.5f, -1.0f, -0.8f);
    std::array<GpuLightData, MaxSceneLights> gpuLights{};
    uint32_t gpuLightCount = 0;
    int32_t shadowLightIndex = -1;
};

struct FrameResolutionParameters {
    float internalToDisplayScale = 1.0f;
    float temporalMipBias = 0.0f;
};

// Shared CPU/HLSL ABI for mesh and fullscreen passes.
struct FrameUniformBuffer {
    TSMat4f model;
    TSMat4f view;
    TSMat4f proj;
    TSVec4f lightDir;
    TSVec4f lightColor;
    TSVec4f camPosAndMetallic;
    TSVec4f roughnessAo;
    TSVec4f uvTransform;
    TSVec4f baseColorFactorAndTexture;
    TSVec4f materialEmission;
    TSVec4f materialRimColorAndStrength;
    TSVec4f materialRimParams;
    TSVec4f lightMeta;
    std::array<GpuLightData, MaxLegacyUniformLights> lights;
    TSMat4f lightViewProj;
    TSVec4f shadowParams;
    TSVec4f advancedLightingParams;
    TSVec4f pcssParams;
    TSVec4f ssaoParams;
    TSVec4f postEffectParams;
    TSVec4f ssrParams;
    TSMat4f previousView;
    TSMat4f previousProj;
    TSMat4f previousModel;
    TSVec4f taaParams;
    std::array<TSMat4f, ShadowCascadeCount> csmLightViewProj;
    TSVec4f csmSplits;
    TSVec4f csmParams;
    std::array<TSVec4f, ShadowCascadeCount> vsmPageTable;
    TSVec4f vsmParams;
};

struct SkyFrameUniformBuffer {
    TSMat4f view;
    TSMat4f proj;
};

struct ShadowPassConstants {
    TSMat4f unusedModel;
    TSMat4f view;
    TSMat4f projection;
};

struct TemporalPassConstants {
    // x history valid, y history weight, zw internal/display scale.
    TSVec4f parameters = TSVec4f(0.0f);
    // xy current-minus-previous projection jitter in screen UV units.
    TSVec4f jitter = TSVec4f(0.0f);
};
static_assert(sizeof(TemporalPassConstants) == 32);

struct BloomPassConstants {
    // x first prefilter level, y threshold, z intensity, w radius.
    TSVec4f parameters = TSVec4f(0.0f);
};

struct SsaoPassConstants {
    // x screen radius, y intensity, z world radius, w normal bias.
    TSVec4f parameters = TSVec4f(0.0f);
};

struct ColorGradingPassConstants {
    // x enabled, y blend strength, z exposure, w debug-output bypass.
    TSVec4f parameters = TSVec4f(0.0f);
    // xy inverse display extent, z aspect ratio, w CAS strength.
    TSVec4f resolutionAndSharpening = TSVec4f(0.0f);
    // x chromatic aberration pixels, y vignette strength,
    // z vignette power, w display-encoded dither strength.
    TSVec4f lensAndOutput = TSVec4f(0.0f);
    // xy vignette center, z vignette radius, w reserved.
    TSVec4f vignetteGeometry = TSVec4f(0.0f);
    // rgb vignette tint, w Bloom enabled.
    TSVec4f vignetteColorAndBloom = TSVec4f(0.0f);
    // x Bloom intensity, y LUT input exposure compensation in EV, zw reserved.
    TSVec4f bloom = TSVec4f(0.0f);
};

struct DlssNrPreparePassConstants {
    // xy display extent, zw current-minus-previous projection jitter in UV.
    TSVec4f displayExtentAndJitter = TSVec4f(0.0f);
};

struct DlssNrPassConstants {
    // x style, y intensity, z local tone, w local structure.
    TSVec4f appearance = TSVec4f(0.0f);
    // x skin structure, y automatic mask, z UI correction, w reset history.
    TSVec4f options = TSVec4f(0.0f);
};

struct LightingPassConstants {
    TSMat4f lightViewProjection;
    TSVec4f shadowParameters;
    TSVec4f featureFlags;
    TSVec4f pcssParameters;
    std::array<TSMat4f, ShadowCascadeCount> cascadeViewProjections;
    TSVec4f cascadeSplits;
    TSVec4f cascadeParameters;
    std::array<TSVec4f, ShadowCascadeCount> virtualShadowPages;
    TSVec4f virtualShadowParameters;
};

class FrameParameterBuilder {
public:
    static FrameLightingParameters buildLighting(const Render::Scene& scene);
    static FrameResolutionParameters buildResolution(
        uint32_t internalWidth,
        uint32_t internalHeight,
        uint32_t displayWidth,
        uint32_t displayHeight,
        int temporalMode,
        float temporalMipBiasAdjustment);

    static void populateMaterialAndLighting(
        FrameUniformBuffer& uniform,
        const std::shared_ptr<Render::Material>& material,
        const FrameLightingParameters& lighting,
        const ShadowViewData& shadowViews,
        const RendererSettings& settings,
        bool environmentLightingEnabled);
};

static_assert(offsetof(FrameUniformBuffer, uvTransform) == 256);
static_assert(offsetof(FrameUniformBuffer, baseColorFactorAndTexture) == 272);
static_assert(sizeof(FrameUniformBuffer) == 1600);
static_assert(sizeof(ShadowPassConstants) == 192);
static_assert(sizeof(BloomPassConstants) == 16);
static_assert(sizeof(SsaoPassConstants) == 16);
static_assert(sizeof(ColorGradingPassConstants) == 96);
static_assert(sizeof(DlssNrPreparePassConstants) == 16);
static_assert(sizeof(DlssNrPassConstants) == 32);
static_assert(offsetof(LightingPassConstants, lightViewProjection) == 0);
static_assert(offsetof(LightingPassConstants, shadowParameters) == 64);
static_assert(offsetof(LightingPassConstants, featureFlags) == 80);
static_assert(offsetof(LightingPassConstants, pcssParameters) == 96);
static_assert(offsetof(LightingPassConstants, cascadeViewProjections) == 112);
static_assert(offsetof(LightingPassConstants, cascadeSplits) == 368);
static_assert(offsetof(LightingPassConstants, cascadeParameters) == 384);
static_assert(offsetof(LightingPassConstants, virtualShadowPages) == 400);
static_assert(offsetof(LightingPassConstants, virtualShadowParameters) == 464);
static_assert(sizeof(LightingPassConstants) == 480);

} // namespace Tasrovy::Renderer
