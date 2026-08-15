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

inline constexpr size_t MaxSceneLights = 8;

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
    std::array<GpuLightData, MaxSceneLights> lights;
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
};

struct BloomPassConstants {
    // x first prefilter level, y threshold, z intensity, w radius.
    TSVec4f parameters = TSVec4f(0.0f);
};

struct SsaoPassConstants {
    // x screen radius, y intensity, z world radius, w normal bias.
    TSVec4f parameters = TSVec4f(0.0f);
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
        int temporalMode);

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
static_assert(sizeof(LightingPassConstants) == 480);

} // namespace Tasrovy::Renderer
