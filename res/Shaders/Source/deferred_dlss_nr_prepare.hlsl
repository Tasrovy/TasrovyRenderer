cbuffer DLSSNRPrepareConstants : register(b0, space0)
{
    // xy display extent, zw current-minus-previous projection jitter in UV.
    float4 displayExtentAndJitter;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct PrepareOutput
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float depth : SV_Target2;
};

[[vk::combinedImageSampler]] Texture2D colorGradedLinear
    : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState colorGradedLinearSampler
    : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D gBufferVelocity
    : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferVelocitySampler
    : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D sceneDepth
    : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler
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

float3 LinearToSrgb(float3 linearColor)
{
    linearColor = max(linearColor, 0.0f.xxx);
    const float3 low = linearColor * 12.92f;
    const float3 high =
        1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, linearColor <= 0.0031308f.xxx);
}

PrepareOutput PSMain(VSOutput input)
{
    PrepareOutput output;
    const float4 linearColor = colorGradedLinear.SampleLevel(
        colorGradedLinearSampler, input.uv, 0.0f);
    output.color = float4(
        saturate(LinearToSrgb(linearColor.rgb)), linearColor.a);

    // GBuffer motion is current-minus-previous in UV units and includes the
    // projection jitter. Feature 18 consumes current-pixel-to-previous-pixel
    // displacement, so remove jitter and reverse the direction.
    const float2 currentMinusPreviousUv = gBufferVelocity.SampleLevel(
        gBufferVelocitySampler, input.uv, 0.0f).xy -
        displayExtentAndJitter.zw;
    output.motion =
        -currentMinusPreviousUv * displayExtentAndJitter.xy;

    // The renderer uses conventional 0-near/1-far depth. The future NGX
    // backend must therefore publish DepthInverted = false.
    output.depth = sceneDepth.SampleLevel(
        sceneDepthSampler, input.uv, 0.0f).r;
    return output;
}
