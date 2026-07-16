cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
    float4 lightColor;
    float4 camPosAndMetallic;
    float4 roughnessAo;
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

float4 PSMain(VSOutput input) : SV_Target
{
    float4 sampledColor = sceneColor.Sample(sceneColorSampler, input.uv);
    return float4(saturate(sampledColor.rgb), 1.0f);
}
