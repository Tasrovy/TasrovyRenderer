#ifndef TASROVY_POSTPROCESS_OUTLINE
#define TASROVY_POSTPROCESS_OUTLINE

float3 ApplyNormalOutline(float2 uv, float3 color)
{
    if (lightDir.w < 0.5f) {
        return color;
    }

    uint width;
    uint height;
    gBufferNormal.GetDimensions(width, height);
    float2 texelSize = rcp(float2(width, height)) * lightColor.y;
    float3 centerEncoded =
        gBufferNormal.SampleLevel(gBufferNormalSampler, uv, 0.0f).rgb;
    float3 centerNormal = normalize(centerEncoded * 2.0f - 1.0f);
    float centerValid = step(0.001f, dot(centerEncoded, centerEncoded));
    static const float2 directions[8] = {
        float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
        float2(0.0f, 1.0f), float2(0.0f, -1.0f),
        float2(0.7071f, 0.7071f), float2(-0.7071f, 0.7071f),
        float2(0.7071f, -0.7071f), float2(-0.7071f, -0.7071f)
    };
    float normalEdge = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 sampleUv = saturate(uv + directions[sampleIndex] * texelSize);
        float3 neighborEncoded =
            gBufferNormal.SampleLevel(gBufferNormalSampler, sampleUv, 0.0f).rgb;
        float3 neighborNormal = normalize(neighborEncoded * 2.0f - 1.0f);
        float neighborValid = step(0.001f, dot(neighborEncoded, neighborEncoded));
        float difference = 1.0f - saturate(dot(centerNormal, neighborNormal));
        difference = max(difference, abs(centerValid - neighborValid));
        normalEdge = max(normalEdge, difference);
    }
    float outline = smoothstep(
        lightColor.x,
        lightColor.x + max(lightColor.w, 0.0001f),
        normalEdge) * lightColor.z;
    return lerp(color, lightDir.rgb, saturate(outline));
}

#endif
