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
    // z contains the requested sample count.
    float4 pcssParams;
    float4 ssaoParams;
    // x strength, y maximum radius in display pixels, zw current-minus-
    // previous projection jitter in normalized screen coordinates.
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
[[vk::combinedImageSampler]] Texture2D gBufferVelocity : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferVelocitySampler : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s3, space0);

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
    const float2 uv = input.uv;
    const float3 centerColor = sceneColor.SampleLevel(
        sceneColorSampler, uv, 0.0f).rgb;
    uint width;
    uint height;
    sceneColor.GetDimensions(width, height);
    const float2 displaySize = float2(width, height);

    float2 velocity = gBufferVelocity.SampleLevel(
        gBufferVelocitySampler, uv, 0.0f).xy;
    velocity -= postEffectParams.zw;

    const float speedPixels = length(velocity * displaySize);
    const float blurPixels = min(
        speedPixels * max(postEffectParams.x, 0.0f),
        max(postEffectParams.y, 0.0f));
    if (blurPixels < 0.5f) {
        return float4(centerColor, 1.0f);
    }

    const float2 blurVelocity =
        normalize(velocity * displaySize) * blurPixels / displaySize;
    const uint sampleCount = clamp(
        (uint)round(pcssParams.z), 4u, 16u);
    const float centerDepth = sceneDepth.SampleLevel(
        sceneDepthSampler, uv, 0.0f).r;
    const float depthTolerance = max(0.0015f, abs(centerDepth) * 0.01f);

    float3 accumulated = 0.0f.xxx;
    float accumulatedWeight = 0.0f;
    [loop]
    for (uint i = 0; i < 16; ++i) {
        if (i >= sampleCount) {
            break;
        }
        const float t =
            ((float)i + 0.5f) / (float)sampleCount - 0.5f;
        const float2 sampleUv = saturate(uv + blurVelocity * t);
        const float sampleDepth = sceneDepth.SampleLevel(
            sceneDepthSampler, sampleUv, 0.0f).r;
        const float depthWeight = saturate(
            1.0f - abs(sampleDepth - centerDepth) / depthTolerance);
        accumulated += sceneColor.SampleLevel(
            sceneColorSampler, sampleUv, 0.0f).rgb * depthWeight;
        accumulatedWeight += depthWeight;
    }
    return float4(
        accumulated / max(accumulatedWeight, 1.0e-4f),
        1.0f);
}
