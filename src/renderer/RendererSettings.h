#pragma once

#include "TSVector.h"

#include <string>

namespace Tasrovy::Renderer {

enum class ShadowTechnique : int {
    ShadowMap = 0,
    CascadedShadowMap = 1,
    VirtualShadowMap = 2
};

enum class DebugTextureSemantic : int {
    FinalOutput = 0,
    Color = 1,
    OutlineBlackLines = 2,
    Normal = 3,
    Velocity = 4,
    RawDepth = 5,
    SceneLinearDepth = 6,
    HiZLinearDepth = 7,
    Mask = 8
};

// User-facing renderer policy. This is deliberately owned by the renderer
// module rather than RHI: an RHI executes work, but does not decide which
// lighting, shadowing, temporal, or post-processing features are enabled.
struct RendererSettings {
    int selectedPipelineIndex = 1;

    std::string debugOutputResource;
    DebugTextureSemantic debugOutputSemantic =
        DebugTextureSemantic::FinalOutput;
    float debugVelocityScale = 32.0f;
    float debugDepthRange = 50.0f;

    float shadowSlopeBias = 0.003f;
    float shadowMinimumBias = 0.0005f;
    float shadowStrength = 1.0f;
    int shadowTechnique = static_cast<int>(ShadowTechnique::VirtualShadowMap);
    float csmMaximumDistance = 50.0f;
    float csmSplitLambda = 0.65f;
    float csmBlendFraction = 0.10f;
    bool pcssEnabled = true;
    float pcssLightSize = 0.018f;
    float pcssMaxFilterRadius = 0.04f;

    bool bloomEnabled = true;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.25f;
    float bloomRadius = 1.0f;
    float exposure = 1.0f;

    bool depthOfFieldEnabled = false;
    float dofFocusDistance = 5.0f;
    float dofFocusRange = 1.5f;
    float dofMaxBlurRadius = 8.0f;
    float dofStrength = 1.0f;

    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.6f;
    float motionBlurMaxRadius = 24.0f;
    int motionBlurSamples = 8;

    // 0: fixed-resolution spatial upscale.
    // 1: native-resolution TAA.
    // 2: fixed internal resolution with temporal upscale.
    int temporalAAMode = 2;
    float taaHistoryWeight = 0.9f;

    bool ssaoEnabled = true;
    float ssaoRadiusPixels = 12.0f;
    float ssaoIntensity = 1.0f;
    float ssaoWorldRadius = 0.75f;
    float ssaoBias = 0.02f;

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
    Tasrovy::Base::TSVec3f outlineColor =
        Tasrovy::Base::TSVec3f(0.02f, 0.015f, 0.02f);
    bool outlineTemporalDenoise = true;
    float outlineHistoryWeight = 0.85f;

    float internalResolutionPercent = 100.0f;
};

} // namespace Tasrovy::Renderer
