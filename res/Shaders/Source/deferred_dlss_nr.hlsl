cbuffer DLSSNRConstants : register(b0, space0)
{
    // x style, y intensity, z local tone, w local structure.
    float4 appearance;
    // x skin structure, y automatic mask, z UI correction, w reset history.
    float4 options;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D dlssNrInputColor
    : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState dlssNrInputColorSampler
    : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D dlssNrMotionVectors
    : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState dlssNrMotionVectorsSampler
    : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D dlssNrDepth
    : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState dlssNrDepthSampler
    : register(s3, space0);

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

float4 PSMain(VSOutput input) : SV_Target
{
    // Temporary fallback. VulkanFrameExecutor will replace this fullscreen
    // draw with NGX Feature 18 evaluation when the runtime is installed.
    const int2 pixel = int2(input.position.xy);
    return dlssNrInputColor.Load(int3(pixel, 0));
}
