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
    // z debug output enabled, w bloom radius.
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
    float4 ssdoParams;
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
[[vk::combinedImageSampler]] Texture2D gBufferWorldPos : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState gBufferWorldPosSampler : register(s3, space0);
[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s4, space0);
[[vk::combinedImageSampler]] Texture2D gBufferMaterial : register(t5, space0);
[[vk::combinedImageSampler]] SamplerState gBufferMaterialSampler : register(s5, space0);
[[vk::combinedImageSampler]] Texture2D hiZHalf : register(t6, space0);
[[vk::combinedImageSampler]] SamplerState hiZHalfSampler : register(s6, space0);
[[vk::combinedImageSampler]] Texture2D hiZQuarter : register(t7, space0);
[[vk::combinedImageSampler]] SamplerState hiZQuarterSampler : register(s7, space0);
[[vk::combinedImageSampler]] Texture2D hiZEighth : register(t8, space0);
[[vk::combinedImageSampler]] SamplerState hiZEighthSampler : register(s8, space0);
[[vk::combinedImageSampler]] Texture2D hiZSixteenth : register(t9, space0);
[[vk::combinedImageSampler]] SamplerState hiZSixteenthSampler : register(s9, space0);
[[vk::combinedImageSampler]] Texture2D taaHistoryColor : register(t10, space0);
[[vk::combinedImageSampler]] SamplerState taaHistoryColorSampler : register(s10, space0);
[[vk::combinedImageSampler]] Texture2D taaHistoryDepth : register(t11, space0);
[[vk::combinedImageSampler]] SamplerState taaHistoryDepthSampler : register(s11, space0);
[[vk::combinedImageSampler]] Texture2D gBufferAlbedo : register(t12, space0);
[[vk::combinedImageSampler]] SamplerState gBufferAlbedoSampler : register(s12, space0);
[[vk::combinedImageSampler]] Texture2D bloomLowRes : register(t13, space0);
[[vk::combinedImageSampler]] SamplerState bloomLowResSampler : register(s13, space0);

#include "PostProcess/postprocess_taa.hlsli"
#if TASROVY_POST_SSR
#include "PostProcess/postprocess_ssr.hlsli"
#endif
#if TASROVY_POST_OUTLINE
#include "PostProcess/postprocess_outline.hlsli"
#endif
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
    if (roughnessAo.z > 0.5f) {
        return float4(saturate(sourceColor), 1.0f);
    }

    float3 color = ApplyTemporalAA(input.uv, sourceColor);
#if TASROVY_POST_SSR
    color = ApplyScreenSpaceReflection(input.uv, color);
#endif
#if TASROVY_POST_BLOOM
    if (uvTransform.x > 0.5f) {
        color += bloomLowRes.SampleLevel(
            bloomLowResSampler, input.uv, 0.0f).rgb * uvTransform.z;
    }
#endif
    color = ApplyExposureToneMap(color, uvTransform.w);
#if TASROVY_POST_OUTLINE
    color = ApplyNormalOutline(input.uv, color);
#endif
    return float4(saturate(color), 1.0f);
}
