cbuffer SsaoPassConstants : register(b0, space0)
{
    // x screen radius, y intensity, z world radius, w normal bias.
    float4 ssaoParams;
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
[[vk::combinedImageSampler]] Texture2D gBufferNormal : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState gBufferNormalSampler : register(s3, space0);

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

float Hash12(float2 value)
{
    float3 p3 = frac(float3(value.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float2 PSMain(VSOutput input) : SV_Target0
{
    const float centerDepth = sceneDepth.SampleLevel(
        sceneDepthSampler, input.uv, 0.0f).r;
    if (centerDepth >= 0.99999f) {
        return 1.0f.xx;
    }

    const float3 centerPosition = gBufferWorldPos.SampleLevel(
        gBufferWorldPosSampler, input.uv, 0.0f).xyz;
    const float3 normal = normalize(
        gBufferNormal.SampleLevel(gBufferNormalSampler, input.uv, 0.0f).xyz * 2.0f - 1.0f);
    uint width;
    uint height;
    sceneDepth.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));
    const float rotation = Hash12(input.position.xy) * 6.28318530718f;
    const float2 baseDirection = float2(cos(rotation), sin(rotation));
    const float2 perpendicular = float2(-baseDirection.y, baseDirection.x);
    const float2 directions[4] = {
        baseDirection, perpendicular, -baseDirection, -perpendicular
    };

    float horizonOcclusion = 0.0f;
    [unroll]
    for (int directionIndex = 0; directionIndex < 4; ++directionIndex) {
        float directionHorizon = 0.0f;
        [unroll]
        for (int stepIndex = 1; stepIndex <= 4; ++stepIndex) {
            const float stepScale = (float)stepIndex * 0.25f;
            const float2 sampleUv = saturate(
                input.uv + directions[directionIndex] * texelSize * ssaoParams.x * stepScale);
            const float sampleDepth = sceneDepth.SampleLevel(
                sceneDepthSampler, sampleUv, 0.0f).r;
            if (sampleDepth >= 0.99999f) {
                continue;
            }
            const float3 samplePosition = gBufferWorldPos.SampleLevel(
                gBufferWorldPosSampler, sampleUv, 0.0f).xyz;
            const float3 delta = samplePosition - centerPosition;
            const float distanceToSample = length(delta);
            if (distanceToSample < 0.0001f || distanceToSample > ssaoParams.z) {
                continue;
            }
            const float horizon = saturate(
                dot(normal, delta / distanceToSample) - ssaoParams.w);
            const float attenuation = 1.0f - saturate(distanceToSample / ssaoParams.z);
            directionHorizon = max(directionHorizon, horizon * attenuation);
        }
        horizonOcclusion += directionHorizon;
    }

    const float ao = saturate(1.0f - horizonOcclusion * 0.25f * ssaoParams.y);
    return ao.xx;
}
