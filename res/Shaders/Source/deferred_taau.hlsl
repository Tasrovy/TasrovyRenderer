cbuffer TemporalPassConstants : register(b0, space0)
{
    // x: enabled and history valid, y: base history weight,
    // zw: internal-resolution / display-resolution scale.
    float4 taaParams;
    // xy: current-minus-previous jitter, zw: current jitter in screen UV units.
    float4 jitterParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D sceneColor : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState sceneColorSampler : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D taaHistoryColor : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState taaHistoryColorSampler : register(s3, space0);
[[vk::combinedImageSampler]] Texture2D taaHistoryData : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState taaHistoryDataSampler : register(s4, space0);
[[vk::combinedImageSampler]] Texture2D gBufferVelocity : register(t5, space0);
[[vk::combinedImageSampler]] SamplerState gBufferVelocitySampler : register(s5, space0);
[[vk::combinedImageSampler]] Texture2D gBufferNormal : register(t6, space0);
[[vk::combinedImageSampler]] SamplerState gBufferNormalSampler : register(s6, space0);
[[vk::combinedImageSampler]] Texture2D gBufferMaterial : register(t7, space0);
[[vk::combinedImageSampler]] SamplerState gBufferMaterialSampler : register(s7, space0);

struct PSOutput
{
    float4 color : SV_Target0;
    // xyz: encoded world normal, w: device depth.
    float4 historyData : SV_Target1;
};

PSOutput MakeTemporalOutput(float3 color, float3 encodedNormal, float depth)
{
    PSOutput output;
    output.color = float4(color, 1.0f);
    output.historyData = float4(encodedNormal, depth);
    return output;
}

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

float3 RGBToYCoCg(float3 color)
{
    return float3(
        dot(color, float3(0.25f, 0.5f, 0.25f)),
        dot(color, float3(0.5f, 0.0f, -0.5f)),
        dot(color, float3(-0.25f, 0.5f, -0.25f)));
}

float3 YCoCgToRGB(float3 color)
{
    return float3(
        color.x + color.y - color.z,
        color.x + color.z,
        color.x - color.y - color.z);
}

float3 SampleCatmullRom(
    Texture2D textureObject,
    SamplerState textureSampler,
    float2 uv,
    float2 textureSize)
{
    const float2 samplePosition = uv * textureSize;
    const float2 texelCenter = floor(samplePosition - 0.5f) + 0.5f;
    const float2 f = samplePosition - texelCenter;

    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);
    const float2 w12 = w1 + w2;
    const float2 offset12 = w2 / max(w12, 1.0e-5f.xx);

    const float2 texel0 = texelCenter - 1.0f;
    const float2 texel12 = texelCenter + offset12;
    const float2 texel3 = texelCenter + 2.0f;
    const float2 invTextureSize = rcp(textureSize);

    float3 result = 0.0f.xxx;
    result += textureObject.SampleLevel(
        textureSampler, float2(texel0.x, texel0.y) * invTextureSize, 0.0f).rgb *
        (w0.x * w0.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel12.x, texel0.y) * invTextureSize, 0.0f).rgb *
        (w12.x * w0.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel3.x, texel0.y) * invTextureSize, 0.0f).rgb *
        (w3.x * w0.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel0.x, texel12.y) * invTextureSize, 0.0f).rgb *
        (w0.x * w12.y);
    result += textureObject.SampleLevel(
        textureSampler, texel12 * invTextureSize, 0.0f).rgb *
        (w12.x * w12.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel3.x, texel12.y) * invTextureSize, 0.0f).rgb *
        (w3.x * w12.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel0.x, texel3.y) * invTextureSize, 0.0f).rgb *
        (w0.x * w3.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel12.x, texel3.y) * invTextureSize, 0.0f).rgb *
        (w12.x * w3.y);
    result += textureObject.SampleLevel(
        textureSampler, float2(texel3.x, texel3.y) * invTextureSize, 0.0f).rgb *
        (w3.x * w3.y);
    return max(result, 0.0f.xxx);
}

float3 SampleSupersampled(
    Texture2D textureObject,
    SamplerState textureSampler,
    float2 uv,
    float2 textureSize,
    float2 internalToDisplayScale)
{
    // Four samples cover the display-pixel footprint in the supersampled
    // source. This is intentionally compact because temporal accumulation
    // provides the remaining integration across frames.
    const float2 offset =
        0.25f * max(internalToDisplayScale, 1.0f.xx) / textureSize;
    float3 color = 0.0f.xxx;
    color += textureObject.SampleLevel(
        textureSampler, uv + float2(-offset.x, -offset.y), 0.0f).rgb;
    color += textureObject.SampleLevel(
        textureSampler, uv + float2( offset.x, -offset.y), 0.0f).rgb;
    color += textureObject.SampleLevel(
        textureSampler, uv + float2(-offset.x,  offset.y), 0.0f).rgb;
    color += textureObject.SampleLevel(
        textureSampler, uv + float2( offset.x,  offset.y), 0.0f).rgb;
    return color * 0.25f;
}

float4 LoadPoint(Texture2D textureObject, float2 uv)
{
    uint width;
    uint height;
    textureObject.GetDimensions(width, height);
    const uint2 coordinate = min(
        uint2(saturate(uv) * float2(width, height)),
        uint2(width - 1u, height - 1u));
    return textureObject.Load(int3(coordinate, 0));
}

float3 DecodeNormal(float3 encodedNormal)
{
    const float3 normal = encodedNormal * 2.0f - 1.0f;
    return normal * rsqrt(max(dot(normal, normal), 1.0e-6f));
}

float4 SelectHistoryData(
    float2 historyUv,
    float validationDepth,
    float3 validationNormal,
    float depthThreshold,
    out float selectedNormalSimilarity)
{
    uint historyWidth;
    uint historyHeight;
    taaHistoryData.GetDimensions(historyWidth, historyHeight);
    const int2 historyExtent = int2(historyWidth, historyHeight);
    const float2 historyTexelPosition =
        historyUv * float2(historyWidth, historyHeight) - 0.5f;
    const int2 baseCoordinate = int2(floor(historyTexelPosition));

    float bestScore = 1.0e20f;
    float4 bestData = 0.0f.xxxx;
    selectedNormalSimilarity = -1.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            const int2 coordinate = clamp(
                baseCoordinate + int2(x, y),
                int2(0, 0),
                historyExtent - 1);
            const float4 candidate =
                taaHistoryData.Load(int3(coordinate, 0));
            const float3 candidateNormal = DecodeNormal(candidate.xyz);
            const float normalSimilarity =
                dot(validationNormal, candidateNormal);
            const float normalizedDepthError =
                abs(validationDepth - candidate.w) /
                max(depthThreshold, 1.0e-6f);
            // Depth identifies the same surface first; the normal resolves
            // candidates with similar depth around silhouettes and corners.
            const float score = normalizedDepthError * 4.0f +
                (1.0f - saturate(normalSimilarity));
            if (score < bestScore)
            {
                bestScore = score;
                bestData = candidate;
                selectedNormalSimilarity = normalSimilarity;
            }
        }
    }
    return bestData;
}

float3 ClipHistoryToAabb(
    float3 historyColor,
    float3 neighborhoodCenter,
    float3 neighborhoodMin,
    float3 neighborhoodMax)
{
    // Intersect the ray from the neighborhood center to the history sample
    // with the color box. Unlike a component-wise clamp this preserves the
    // direction of the luminance/chroma correction and avoids hue shifts.
    const float3 center = clamp(
        neighborhoodCenter, neighborhoodMin, neighborhoodMax);
    const float3 direction = historyColor - center;
    const float3 distanceToBoundary = float3(
        direction.x >= 0.0f
            ? neighborhoodMax.x - center.x
            : center.x - neighborhoodMin.x,
        direction.y >= 0.0f
            ? neighborhoodMax.y - center.y
            : center.y - neighborhoodMin.y,
        direction.z >= 0.0f
            ? neighborhoodMax.z - center.z
            : center.z - neighborhoodMin.z);
    const float3 exitDistance = distanceToBoundary /
        max(abs(direction), 1.0e-5f.xxx);
    const float clipAmount = saturate(min(
        exitDistance.x, min(exitDistance.y, exitDistance.z)));
    return center + direction * clipAmount;
}

PSOutput PSMain(VSOutput input)
{
    const float2 uv = input.uv;
    uint internalWidth;
    uint internalHeight;
    sceneColor.GetDimensions(internalWidth, internalHeight);
    const float2 internalSize = float2(internalWidth, internalHeight);
    const bool isUpscaling = any(taaParams.zw < 0.999f.xx);
    const bool isDownscaling = any(taaParams.zw > 1.001f.xx);
    // Projection jitter displaces the low-resolution source image. Resolve
    // that displacement while reconstructing it onto the stable display grid.
    // Native TAA intentionally keeps sampling the current pixel center.
    const float2 currentSampleUv = isUpscaling
        ? uv + jitterParams.zw
        : uv;
    const float3 currentColor = isUpscaling
        ? SampleCatmullRom(
            sceneColor, sceneColorSampler, currentSampleUv, internalSize)
        : isDownscaling
            ? SampleSupersampled(
                sceneColor, sceneColorSampler, uv, internalSize, taaParams.zw)
            : sceneColor.SampleLevel(sceneColorSampler, uv, 0.0f).rgb;
    const float currentDepth = LoadPoint(sceneDepth, currentSampleUv).r;
    const float3 currentEncodedNormal =
        LoadPoint(gBufferNormal, currentSampleUv).xyz;
    if (taaParams.x < 0.5f) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    // Dilate foreground velocity into geometric edges by choosing the closest
    // depth in a 3x3 neighborhood. Bilinear velocity at silhouettes mixes
    // unrelated foreground/background motion and is a major source of trails.
    float closestDepth = currentDepth;
    float2 closestUv = currentSampleUv;
    uint foregroundSampleCount = currentDepth < 0.999999f ? 1u : 0u;
    const float2 internalTexelSize = rcp(internalSize);
    [unroll]
    for (int velocityY = -1; velocityY <= 1; ++velocityY) {
        [unroll]
        for (int velocityX = -1; velocityX <= 1; ++velocityX) {
            const float2 candidateUv = currentSampleUv +
                float2(velocityX, velocityY) * internalTexelSize;
            const float candidateDepth = LoadPoint(sceneDepth, candidateUv).r;
            if ((velocityX != 0 || velocityY != 0) &&
                candidateDepth < 0.999999f) {
                ++foregroundSampleCount;
            }
            if (candidateDepth < closestDepth) {
                closestDepth = candidateDepth;
                closestUv = candidateUv;
            }
        }
    }
    // A footprint containing both scene geometry and the far plane is a
    // likely sub-pixel silhouette. Projection jitter can make its center
    // alternate between foreground and background even at native resolution.
    const bool mixedGeometryCoverage =
        foregroundSampleCount > 0u && foregroundSampleCount < 9u;
    // Treat both directions of a coverage transition identically. Always use
    // the closest foreground sample as the geometric representative whenever
    // the current footprint mixes foreground and background.
    const bool subpixelGeometryEdge = mixedGeometryCoverage;
    const float validationDepth = subpixelGeometryEdge
        ? closestDepth
        : currentDepth;
    const float2 validationUv = subpixelGeometryEdge
        ? closestUv
        : currentSampleUv;
    const float3 validationEncodedNormal = subpixelGeometryEdge
        ? LoadPoint(gBufferNormal, closestUv).xyz
        : currentEncodedNormal;
    // GBuffer velocity contains projection jitter. Temporal history has
    // already been resolved onto the stable output grid, so reproject it with
    // physical camera/object motion only.
    const float2 velocity =
        LoadPoint(gBufferVelocity, closestUv).xy - jitterParams.xy;
    const float2 historyUv = uv - velocity;
    if (any(historyUv <= 0.0f.xx) || any(historyUv >= 1.0f.xx)) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    const float depthThreshold = max(
        subpixelGeometryEdge ? 0.003f : 0.0015f,
        abs(validationDepth) *
            (subpixelGeometryEdge ? 0.02f : 0.01f));
    const float3 currentNormal = DecodeNormal(validationEncodedNormal);
    float historyNormalSimilarity;
    const float4 historyData = SelectHistoryData(
        historyUv,
        validationDepth,
        currentNormal,
        depthThreshold,
        historyNormalSimilarity);
    const float historyDepth = historyData.w;
    if (abs(validationDepth - historyDepth) > depthThreshold) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    const float normalThreshold = subpixelGeometryEdge ? 0.55f : 0.75f;
    if (historyNormalSimilarity < normalThreshold) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    // A 3x3 source neighborhood is wide enough to cover the sub-pixel phase
    // changes introduced by projection jitter. The previous 2x2 gather made
    // small bright emitters repeatedly enter and leave the clamp bounds.
    float3 neighborhoodMin = RGBToYCoCg(currentColor);
    float3 neighborhoodMax = neighborhoodMin;
    float3 neighborhoodMean = 0.0f.xxx;
    float3 neighborhoodMoment2 = 0.0f.xxx;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float3 sampleColor = RGBToYCoCg(sceneColor.SampleLevel(
                sceneColorSampler, currentSampleUv, 0.0f, int2(x, y)).rgb);
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
            neighborhoodMean += sampleColor;
            neighborhoodMoment2 += sampleColor * sampleColor;
        }
    }
    neighborhoodMean /= 9.0f;
    neighborhoodMoment2 /= 9.0f;
    const float3 neighborhoodSigma = sqrt(max(
        neighborhoodMoment2 - neighborhoodMean * neighborhoodMean,
        0.0f.xxx));
    // A binary foreground/background footprint naturally has high variance.
    // Give valid reprojected foreground a wider clamp only for that footprint;
    // ordinary surfaces retain the sharper clamp used before.
    const float varianceGamma = subpixelGeometryEdge ? 2.5f : 1.75f;
    const float3 varianceMin =
        neighborhoodMean - varianceGamma * neighborhoodSigma;
    const float3 varianceMax =
        neighborhoodMean + varianceGamma * neighborhoodSigma;
    const float3 neighborhoodExtent =
        max(neighborhoodMax - neighborhoodMin, 0.001f.xxx);
    const float clampPadding = subpixelGeometryEdge ? 0.15f : 0.05f;
    neighborhoodMin = max(
        neighborhoodMin - neighborhoodExtent * clampPadding,
        varianceMin);
    neighborhoodMax = min(
        neighborhoodMax + neighborhoodExtent * clampPadding,
        varianceMax);

    uint historyWidth;
    uint historyHeight;
    taaHistoryColor.GetDimensions(historyWidth, historyHeight);
    const float2 historySize = float2(historyWidth, historyHeight);
    float3 historyColor = SampleCatmullRom(
        taaHistoryColor,
        taaHistoryColorSampler,
        historyUv,
        historySize);
    historyColor = YCoCgToRGB(ClipHistoryToAabb(
        RGBToYCoCg(historyColor),
        neighborhoodMean,
        neighborhoodMin,
        neighborhoodMax));

    const float motionPixels = length(
        velocity * historySize);
    const float motionConfidence = saturate(1.0f - motionPixels / 48.0f);
    const float currentLuminance = dot(
        currentColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float historyLuminance = dot(
        historyColor, float3(0.2126f, 0.7152f, 0.0722f));
    const float luminanceScale =
        max(max(currentLuminance, historyLuminance), 0.25f);
    const float luminanceConfidence = saturate(
        1.0f - abs(currentLuminance - historyLuminance) /
        (luminanceScale * 1.5f));
    const float reactiveMask = LoadPoint(
        gBufferMaterial, validationUv).a > (0.5f / 255.0f)
        ? 1.0f
        : 0.0f;
    // Once depth, normal and reprojection agree, a foreground/background
    // luminance jump is expected for a missed sub-pixel sample. Retaining a
    // bounded amount of history turns the binary per-frame coverage into a
    // stable temporal coverage without relaxing rejection across the image.
    const float stableLuminanceConfidence = subpixelGeometryEdge
        ? 1.0f
        : luminanceConfidence;
    const float historyWeight =
        saturate(taaParams.y) * motionConfidence *
        stableLuminanceConfidence * (1.0f - reactiveMask);
    const bool retainForegroundMetadata =
        subpixelGeometryEdge && historyWeight > 0.05f;
    return MakeTemporalOutput(
        lerp(currentColor, historyColor, historyWeight),
        retainForegroundMetadata
            ? validationEncodedNormal
            : currentEncodedNormal,
        retainForegroundMetadata ? validationDepth : currentDepth);
}
