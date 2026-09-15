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
    int shadowTechnique = static_cast<int>(ShadowTechnique::CascadedShadowMap);
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
    bool colorGradingEnabled = true;
    float colorGradingStrength = 1.0f;
    float colorGradingExposureCompensationEv = 0.0f;
    bool finalSharpeningEnabled = true;
    float finalSharpeningStrength = 0.5f;
    float chromaticAberrationPixels = 0.0f;
    float vignetteStrength = 0.0f;
    float vignettePower = 2.0f;
    Tasrovy::Base::TSVec3f vignetteColor =
        Tasrovy::Base::TSVec3f(0.0f);
    float displayDitherStrength = 0.35f;

    // Same-resolution DLSS Neural Rendering. Until the NGX runtime is
    // installed, the render pass remains a color-preserving fallback.
    bool dlssNeuralRenderingEnabled = false;
    int dlssNrStyle = 0;
    float dlssNrIntensity = 1.0f;
    float dlssNrLocalToneStrength = 1.0f;
    float dlssNrLocalStructureStrength = 1.0f;
    float dlssNrSkinStructureStrength = -1.0f;
    bool dlssNrUseAutoMask = false;
    bool dlssNrUiCorrection = false;

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
    // Added to the resolution-derived TAAU bias, then clamped to [-2, 0].
    // Positive values favor blurrier mip levels and reduce sub-pixel shimmer.
    float temporalMipBiasAdjustment = 0.0f;

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

    bool outlineEnabled = false;
    float outlineThreshold = 0.12f;
    float outlineThickness = 1.0f;
    float outlineStrength = 0.85f;
    float outlineSoftness = 0.05f;
    Tasrovy::Base::TSVec3f outlineColor =
        Tasrovy::Base::TSVec3f(0.02f, 0.015f, 0.02f);
    bool outlineTemporalDenoise = false;
    float outlineHistoryWeight = 0.85f;

    float internalResolutionPercent = 100.0f;
};

} // namespace Tasrovy::Renderer
