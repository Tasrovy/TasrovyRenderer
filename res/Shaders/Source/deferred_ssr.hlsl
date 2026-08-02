struct GpuLightData
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 parameters;
};

// Keep this layout byte-identical to SceneRenderer::UniformBufferObject.
cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
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

#include "PostProcess/postprocess_ssr.hlsli"

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
    const float3 baseColor = sceneColor.SampleLevel(
        sceneColorSampler, input.uv, 0.0f).rgb;
    return float4(ApplyScreenSpaceReflection(input.uv, baseColor), 1.0f);
}
