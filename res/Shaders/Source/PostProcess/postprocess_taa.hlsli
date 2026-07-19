#ifndef TASROVY_POSTPROCESS_TAA
#define TASROVY_POSTPROCESS_TAA

float3 ApplyTemporalAA(float2 uv, float3 currentColor)
{
    if (taaParams.x < 0.5f) {
        return currentColor;
    }

    // The stable five-target GBuffer packs signed velocity into two otherwise
    // unused opaque alpha channels: X in albedo.a and Y in normal.a.
    float2 velocity = float2(
        gBufferAlbedo.SampleLevel(gBufferAlbedoSampler, uv, 0.0f).a,
        gBufferNormal.SampleLevel(gBufferNormalSampler, uv, 0.0f).a);
    float2 historyUv = uv - velocity;
    if (any(historyUv <= 0.0f.xx) || any(historyUv >= 1.0f.xx)) {
        return currentColor;
    }

    float currentDepth = sceneDepth.SampleLevel(sceneDepthSampler, uv, 0.0f).r;
    // Velocity is intentionally empty for the sky/background. Rejecting its
    // history prevents a rotating camera from smearing stale sky pixels.
    if (currentDepth >= 0.999999f) {
        return currentColor;
    }
    float historyDepth = taaHistoryDepth.SampleLevel(
        taaHistoryDepthSampler, historyUv, 0.0f).r;
    float depthThreshold = max(0.0015f, abs(currentDepth) * 0.01f);
    if (abs(currentDepth - historyDepth) > depthThreshold) {
        return currentColor;
    }

    // Three hardware gathers fetch the same 2x2 footprint for RGB. Compared
    // with the old 3x3 loop this replaces nine full texture samples with three
    // gather instructions while retaining neighborhood anti-ghosting.
    float4 neighborhoodR = sceneColor.GatherRed(sceneColorSampler, uv);
    float4 neighborhoodG = sceneColor.GatherGreen(sceneColorSampler, uv);
    float4 neighborhoodB = sceneColor.GatherBlue(sceneColorSampler, uv);
    float3 neighborhoodMin = min(
        currentColor,
        float3(
            min(min(neighborhoodR.x, neighborhoodR.y), min(neighborhoodR.z, neighborhoodR.w)),
            min(min(neighborhoodG.x, neighborhoodG.y), min(neighborhoodG.z, neighborhoodG.w)),
            min(min(neighborhoodB.x, neighborhoodB.y), min(neighborhoodB.z, neighborhoodB.w))));
    float3 neighborhoodMax = max(
        currentColor,
        float3(
            max(max(neighborhoodR.x, neighborhoodR.y), max(neighborhoodR.z, neighborhoodR.w)),
            max(max(neighborhoodG.x, neighborhoodG.y), max(neighborhoodG.z, neighborhoodG.w)),
            max(max(neighborhoodB.x, neighborhoodB.y), max(neighborhoodB.z, neighborhoodB.w))));

    float3 historyColor = taaHistoryColor.SampleLevel(
        taaHistoryColorSampler, historyUv, 0.0f).rgb;
    historyColor = clamp(historyColor, neighborhoodMin, neighborhoodMax);

    uint width;
    uint height;
    sceneColor.GetDimensions(width, height);
    float motionPixels = length(velocity * float2(width, height));
    float motionConfidence = saturate(1.0f - motionPixels / 32.0f);
    float historyWeight = saturate(taaParams.y) * motionConfidence;
    return lerp(currentColor, historyColor, historyWeight);
}

#endif
