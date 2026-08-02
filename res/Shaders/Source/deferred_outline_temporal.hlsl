struct GpuLightData
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 parameters;
};

// Keep this prefix byte-identical to SceneRenderer::UniformBufferObject.
cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    // rgb is unused; w enables edge detection.
    float4 lightDir;
    // threshold, thickness, strength, softness.
    float4 lightColor;
    float4 camPosAndMetallic;
    float4 roughnessAo;
    float4 uvTransform;
    float4 baseColorFactorAndTexture;
    float4 materialEmission;
    float4 materialRimColorAndStrength;
    float4 materialRimParams;
    float4 lightMeta;
    GpuLightData lights[8];
    matrix lightViewProj;
    float4 shadowParams;
    float4 advancedLightingParams;
    float4 pcssParams;
    float4 ssaoParams;
    float4 postEffectParams;
    float4 ssrParams;
    matrix previousView;
    matrix previousProj;
    matrix previousModel;
    // x history valid/enabled, y history weight.
    float4 taaParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D gBufferNormal : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState gBufferNormalSampler : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D gBufferVelocity : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferVelocitySampler : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D outlineHistory : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState outlineHistorySampler : register(s3, space0);
[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s4, space0);

#include "PostProcess/postprocess_outline.hlsli"

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = positions[vertexId] * 0.5f + 0.5f;
    return output;
}

float2 PSMain(VSOutput input) : SV_Target
{
    const float2 uv = input.uv;
    const float currentOutline = CalculateNormalOutline(uv);
    const float currentDepth =
        sceneDepth.SampleLevel(sceneDepthSampler, uv, 0.0f).r;
    if (taaParams.x < 0.5f) {
        return float2(currentOutline, currentDepth);
    }

    uint width;
    uint height;
    outlineHistory.GetDimensions(width, height);
    const float2 historySize = float2(width, height);
    const float2 displayTexel = rcp(historySize);

    // Reuse the GBuffer UV velocity convention used by TAA.
    const float2 velocity = gBufferVelocity.SampleLevel(
        gBufferVelocitySampler, uv, 0.0f).xy;
    const float2 historyUv = uv - velocity;
    if (any(historyUv <= displayTexel) ||
        any(historyUv >= 1.0f.xx - displayTexel)) {
        return float2(currentOutline, currentDepth);
    }

    const float2 history = outlineHistory.SampleLevel(
        outlineHistorySampler, historyUv, 0.0f).rg;
    const float depthThreshold = max(0.0015f, abs(currentDepth) * 0.01f);
    if (abs(currentDepth - history.y) > depthThreshold) {
        return float2(currentOutline, currentDepth);
    }

    // Let one-pixel phase changes retain nearby history while preventing old
    // silhouettes from spreading farther than the current edge neighborhood.
    float neighborhoodMin = currentOutline;
    float neighborhoodMax = currentOutline;
    static const float2 cardinal[4] = {
        float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
        float2(0.0f, 1.0f), float2(0.0f, -1.0f)
    };
    [unroll]
    for (int index = 0; index < 4; ++index) {
        const float neighbor = CalculateNormalOutline(
            saturate(uv + cardinal[index] * displayTexel));
        neighborhoodMin = min(neighborhoodMin, neighbor);
        neighborhoodMax = max(neighborhoodMax, neighbor);
    }

    const float clippedHistory = clamp(
        history.x,
        max(0.0f, neighborhoodMin - 0.1f),
        min(1.0f, neighborhoodMax + 0.1f));
    const float motionPixels = length(velocity * historySize);
    const float motionConfidence = saturate(1.0f - motionPixels / 24.0f);
    const float historyWeight =
        saturate(taaParams.y) * motionConfidence;
    return float2(
        lerp(currentOutline, clippedHistory, historyWeight),
        currentDepth);
}
