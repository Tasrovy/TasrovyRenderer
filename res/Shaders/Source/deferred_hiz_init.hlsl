cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D gBufferWorldPos : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferWorldPosSampler : register(s2, space0);

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

float2 PSMain(VSOutput input) : SV_Target
{
    uint width;
    uint height;
    sceneDepth.GetDimensions(width, height);
    uint2 baseCoord = min(uint2(input.position.xy) * 2u, uint2(width - 1u, height - 1u));
    float minimumDepth = 65504.0f;

    [unroll]
    for (uint y = 0u; y < 2u; ++y) {
        [unroll]
        for (uint x = 0u; x < 2u; ++x) {
            uint2 coord = min(baseCoord + uint2(x, y), uint2(width - 1u, height - 1u));
            float depth = sceneDepth.Load(int3(coord, 0)).r;
            if (depth >= 0.99999f) {
                continue;
            }
            float3 worldPosition = gBufferWorldPos.Load(int3(coord, 0)).xyz;
            float linearDepth = max(-mul(float4(worldPosition, 1.0f), view).z, 0.0f);
            minimumDepth = min(minimumDepth, linearDepth);
        }
    }
    return minimumDepth.xx;
}
