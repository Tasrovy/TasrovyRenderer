cbuffer BloomPassConstants : register(b0, space0)
{
    float4 bloomParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D sceneColor : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState sceneColorSampler : register(s1, space0);

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

float3 ExtractBloom(float3 color)
{
    const float brightness = max(color.r, max(color.g, color.b));
    const float contribution =
        saturate((brightness - bloomParams.y) / max(brightness, 0.0001f));
    return color * contribution;
}

float3 SampleBloomAt(float2 uv)
{
    return ExtractBloom(sceneColor.SampleLevel(
        sceneColorSampler, saturate(uv), 0.0f).rgb);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    uint width;
    uint height;
    sceneColor.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));
    const float2 offset = texelSize * max(bloomParams.w, 0.25f) * 4.0f;
    float3 bloom = 0.0f.xxx;
    bloom += SampleBloomAt(input.uv) * 0.20f;
    bloom += SampleBloomAt(input.uv + float2( offset.x, 0.0f)) * 0.12f;
    bloom += SampleBloomAt(input.uv + float2(-offset.x, 0.0f)) * 0.12f;
    bloom += SampleBloomAt(input.uv + float2(0.0f,  offset.y)) * 0.12f;
    bloom += SampleBloomAt(input.uv + float2(0.0f, -offset.y)) * 0.12f;
    bloom += SampleBloomAt(input.uv + float2( offset.x,  offset.y)) * 0.08f;
    bloom += SampleBloomAt(input.uv + float2(-offset.x,  offset.y)) * 0.08f;
    bloom += SampleBloomAt(input.uv + float2( offset.x, -offset.y)) * 0.08f;
    bloom += SampleBloomAt(input.uv + float2(-offset.x, -offset.y)) * 0.08f;
    return float4(bloom, 1.0f);
}
