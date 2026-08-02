cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
    float4 lightColor;
    float4 camPosAndMetallic;
    // w controls the upsample filter radius.
    float4 roughnessAo;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D lowerBloom : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState lowerBloomSampler : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D currentBloom : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState currentBloomSampler : register(s2, space0);

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

float3 SampleLower(float2 uv)
{
    return lowerBloom.SampleLevel(
        lowerBloomSampler, saturate(uv), 0.0f).rgb;
}

float4 PSMain(VSOutput input) : SV_Target
{
    uint width;
    uint height;
    lowerBloom.GetDimensions(width, height);
    const float2 texel =
        rcp(float2(width, height)) * max(roughnessAo.w, 0.25f);

    // Separable 3x3 tent kernel, equivalent to a compact Gaussian
    // reconstruction filter with normalized [1 2 1] weights.
    float3 blurred = 0.0f.xxx;
    blurred += SampleLower(input.uv + float2(-texel.x, -texel.y)) * 0.0625f;
    blurred += SampleLower(input.uv + float2( 0.0f,    -texel.y)) * 0.1250f;
    blurred += SampleLower(input.uv + float2( texel.x, -texel.y)) * 0.0625f;
    blurred += SampleLower(input.uv + float2(-texel.x,  0.0f)) * 0.1250f;
    blurred += SampleLower(input.uv) * 0.2500f;
    blurred += SampleLower(input.uv + float2( texel.x,  0.0f)) * 0.1250f;
    blurred += SampleLower(input.uv + float2(-texel.x,  texel.y)) * 0.0625f;
    blurred += SampleLower(input.uv + float2( 0.0f,     texel.y)) * 0.1250f;
    blurred += SampleLower(input.uv + float2( texel.x,  texel.y)) * 0.0625f;

    const float3 current = currentBloom.SampleLevel(
        currentBloomSampler, input.uv, 0.0f).rgb;
    return float4(current + blurred * 0.65f, 1.0f);
}
