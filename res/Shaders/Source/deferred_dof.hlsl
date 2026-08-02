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
    float4 pcssParams;
    float4 ssaoParams;
    // x focus distance, y focus range, z maximum radius in pixels,
    // w effect strength.
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
[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D gBufferWorldPos : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState gBufferWorldPosSampler : register(s3, space0);

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
    const float centerDepth = sceneDepth.SampleLevel(
        sceneDepthSampler, uv, 0.0f).r;
    if (centerDepth >= 0.999999f || postEffectParams.w <= 0.0f) {
        return float4(centerColor, 1.0f);
    }

    const float3 worldPosition = gBufferWorldPos.SampleLevel(
        gBufferWorldPosSampler, uv, 0.0f).xyz;
    const float cameraDistance = length(worldPosition - camPosAndMetallic.xyz);
    const float focusDistance = max(postEffectParams.x, 0.05f);
    const float focusRange = max(postEffectParams.y, 0.05f);
    const float coc = saturate(
        abs(cameraDistance - focusDistance) / focusRange * postEffectParams.w);
    const float blurRadius = coc * max(postEffectParams.z, 0.0f);
    if (blurRadius < 0.25f) {
        return float4(centerColor, 1.0f);
    }

    uint width;
    uint height;
    sceneColor.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));
    static const float2 disk[12] = {
        float2( 0.0000f,  0.0000f),
        float2( 0.5278f, -0.0859f),
        float2(-0.0401f,  0.5361f),
        float2(-0.6704f, -0.1799f),
        float2( 0.2396f,  0.6939f),
        float2( 0.7042f,  0.3154f),
        float2(-0.4191f, -0.6160f),
        float2(-0.8069f,  0.3288f),
        float2( 0.3934f, -0.8073f),
        float2( 0.9462f, -0.3156f),
        float2(-0.1537f,  0.9342f),
        float2(-0.9277f, -0.4098f)
    };

    float3 accumulated = 0.0f.xxx;
    float accumulatedWeight = 0.0f;
    [unroll]
    for (uint i = 0; i < 12; ++i) {
        const float2 sampleUv = saturate(
            uv + disk[i] * texelSize * blurRadius);
        const float sampleDepth = sceneDepth.SampleLevel(
            sceneDepthSampler, sampleUv, 0.0f).r;
        // Keep foreground silhouettes from pulling distant background color
        // across their edges while still allowing a soft transition.
        const float depthWeight = sampleDepth + 0.0005f >= centerDepth
            ? 1.0f
            : 0.25f;
        accumulated += sceneColor.SampleLevel(
            sceneColorSampler, sampleUv, 0.0f).rgb * depthWeight;
        accumulatedWeight += depthWeight;
    }
    const float3 blurred = accumulated / max(accumulatedWeight, 1.0e-4f);
    return float4(lerp(centerColor, blurred, coc), 1.0f);
}
