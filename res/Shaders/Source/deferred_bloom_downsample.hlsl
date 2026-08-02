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
    // y threshold.
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
    // w is one for the first bright-prefilter level.
    float4 pcssParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D sourceTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState sourceSampler : register(s1, space0);

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

float3 SampleSource(float2 uv)
{
    return max(sourceTexture.SampleLevel(
        sourceSampler, saturate(uv), 0.0f).rgb, 0.0f.xxx);
}

float3 SoftThreshold(float3 color)
{
    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = max(uvTransform.y, 0.0f);
    const float knee = max(threshold * 0.5f, 0.0001f);
    float soft = clamp(
        brightness - threshold + knee,
        0.0f,
        2.0f * knee);
    soft = soft * soft / (4.0f * knee);
    const float contribution =
        max(soft, brightness - threshold) /
        max(brightness, 0.0001f);
    return color * saturate(contribution);
}

float4 PSMain(VSOutput input) : SV_Target
{
    uint width;
    uint height;
    sourceTexture.GetDimensions(width, height);
    const float2 texel = rcp(float2(width, height));

    // A normalized 13-tap Gaussian-like prefilter integrates the source
    // footprint before reducing resolution, avoiding the old point-grid
    // aliasing on thin emissive silhouettes.
    float3 color = SampleSource(input.uv) * 0.15f;
    color += SampleSource(input.uv + float2( texel.x, 0.0f)) * 0.10f;
    color += SampleSource(input.uv + float2(-texel.x, 0.0f)) * 0.10f;
    color += SampleSource(input.uv + float2(0.0f,  texel.y)) * 0.10f;
    color += SampleSource(input.uv + float2(0.0f, -texel.y)) * 0.10f;
    color += SampleSource(input.uv + float2( texel.x,  texel.y)) * 0.075f;
    color += SampleSource(input.uv + float2(-texel.x,  texel.y)) * 0.075f;
    color += SampleSource(input.uv + float2( texel.x, -texel.y)) * 0.075f;
    color += SampleSource(input.uv + float2(-texel.x, -texel.y)) * 0.075f;
    color += SampleSource(input.uv + float2( 2.0f * texel.x, 0.0f)) * 0.0375f;
    color += SampleSource(input.uv + float2(-2.0f * texel.x, 0.0f)) * 0.0375f;
    color += SampleSource(input.uv + float2(0.0f,  2.0f * texel.y)) * 0.0375f;
    color += SampleSource(input.uv + float2(0.0f, -2.0f * texel.y)) * 0.0375f;

    if (pcssParams.w > 0.5f) {
        color = SoftThreshold(color);
    }
    return float4(color, 1.0f);
}
