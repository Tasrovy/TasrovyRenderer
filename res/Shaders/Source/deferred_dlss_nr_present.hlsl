struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D dlssNrOutputColor
    : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState dlssNrOutputColorSampler
    : register(s1, space0);

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = positions[vertexId] * 0.5f + 0.5f;
    return output;
}

float3 SrgbToLinear(float3 encodedColor)
{
    encodedColor = saturate(encodedColor);
    const float3 low = encodedColor / 12.92f;
    const float3 high = pow(
        (encodedColor + 0.055f) / 1.055f,
        2.4f);
    return lerp(high, low, encodedColor <= 0.04045f.xxx);
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float4 encoded = dlssNrOutputColor.Load(
        int3(int2(input.position.xy), 0));
    // FinalColor is an sRGB swapchain attachment, so output linear values and
    // let the attachment conversion restore the encoded display bytes.
    return float4(SrgbToLinear(encoded.rgb), encoded.a);
}
