// The postprocess implementation is split into reusable HLSL modules, but DXC
// compiles them through this single entry point into one SPIR-V shader and one
// fullscreen draw.

#ifndef TASROVY_POST_SSR
#define TASROVY_POST_SSR 1
#endif
#ifndef TASROVY_POST_BLOOM
#define TASROVY_POST_BLOOM 1
#endif
#ifndef TASROVY_POST_OUTLINE
#define TASROVY_POST_OUTLINE 1
#endif

struct GpuLightData
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 parameters;
};

cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    // Postprocess: rgb outline color, w outline enabled.
    float4 lightDir;
    // Postprocess: threshold, thickness, strength, softness.
    float4 lightColor;
    float4 camPosAndMetallic;
    // z debug semantic: 0 final, 1 color, 2 outline, 3 normal,
    // 4 velocity, 5 raw depth, 6 scene linear depth, 7 Hi-Z, 8 mask.
    float4 roughnessAo;
    // x bloom enabled, y threshold, z intensity, w exposure.
    float4 uvTransform;
    float4 baseColorFactorAndTexture;
    float4 materialEmission;
    float4 materialRimColorAndStrength;
    float4 materialRimParams;
    float4 lightMeta;
    GpuLightData lights[8];
    matrix lightViewProj;
    float4 shadowParams;
    // w SSR enabled.
    float4 advancedLightingParams;
    float4 pcssParams;
    float4 ssaoParams;
    float4 postEffectParams;
    // x max distance, y step size, z thickness, w intensity.
    float4 ssrParams;
    matrix previousView;
    matrix previousProj;
    matrix previousModel;
    // x enabled/history valid, y history weight.
    float4 taaParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D sceneColor : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState sceneColorSampler : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D gBufferNormal : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferNormalSampler : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D bloomLowRes : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState bloomLowResSampler : register(s3, space0);
[[vk::combinedImageSampler]] Texture2D outlineMask : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState outlineMaskSampler : register(s4, space0);
[[vk::combinedImageSampler]] Texture2D gBufferWorldPos : register(t5, space0);
[[vk::combinedImageSampler]] SamplerState gBufferWorldPosSampler : register(s5, space0);

#include "PostProcess/postprocess_tonemap.hlsli"

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

float4 PSMain(VSOutput input) : SV_Target
{
    float3 sourceColor =
        sceneColor.SampleLevel(sceneColorSampler, input.uv, 0.0f).rgb;
    const uint debugSemantic = (uint)round(max(roughnessAo.z, 0.0f));
    if (debugSemantic == 2u) {
#if TASROVY_POST_OUTLINE
        float outline = outlineMask.SampleLevel(
            outlineMaskSampler, input.uv, 0.0f).r;
        return float4((1.0f - outline).xxx, 1.0f);
#else
        return 1.0f.xxxx;
#endif
    }
    if (debugSemantic == 1u) {
        return float4(saturate(sourceColor), 1.0f);
    }
    if (debugSemantic == 3u) {
        const float valid = step(
            0.001f, dot(sourceColor, sourceColor));
        const float3 normal = normalize(
            sourceColor * 2.0f - 1.0f);
        return float4(
            (normal * 0.5f + 0.5f) * valid,
            1.0f);
    }
    if (debugSemantic == 4u) {
        const float2 velocity = sourceColor.rg;
        const float2 mapped = saturate(
            0.5f.xx + velocity * ssaoParams.w);
        return float4(mapped, 0.5f, 1.0f);
    }
    if (debugSemantic == 5u) {
        return float4(saturate(sourceColor.r).xxx, 1.0f);
    }
    if (debugSemantic == 6u) {
        if (sourceColor.r >= 0.999999f) {
            return float4(0.0f.xxx, 1.0f);
        }
        const float3 worldPosition = gBufferWorldPos.SampleLevel(
            gBufferWorldPosSampler, input.uv, 0.0f).xyz;
        const float linearDepth = max(
            -mul(float4(worldPosition, 1.0f), view).z,
            0.0f);
        const float value = 1.0f - saturate(
            linearDepth / max(ssaoParams.z, 0.001f));
        return float4(value.xxx, 1.0f);
    }
    if (debugSemantic == 7u) {
        const float linearDepth = sourceColor.r;
        const float value = linearDepth >= 65500.0f
            ? 0.0f
            : 1.0f - saturate(
                linearDepth / max(ssaoParams.z, 0.001f));
        return float4(value.xxx, 1.0f);
    }
    if (debugSemantic == 8u) {
        return float4(saturate(sourceColor.r).xxx, 1.0f);
    }

    float3 color = sourceColor;
#if TASROVY_POST_BLOOM
    if (uvTransform.x > 0.5f) {
        color += bloomLowRes.SampleLevel(
            bloomLowResSampler, input.uv, 0.0f).rgb * uvTransform.z;
    }
#endif
    color = ApplyExposureToneMap(color, uvTransform.w);
#if TASROVY_POST_OUTLINE
    float outline = outlineMask.SampleLevel(
        outlineMaskSampler, input.uv, 0.0f).r;
    color = lerp(color, lightDir.rgb, saturate(outline));
#endif
    return float4(saturate(color), 1.0f);
}
