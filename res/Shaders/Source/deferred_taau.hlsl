cbuffer TemporalPassConstants : register(b0, space0)
{
    // x: enabled and history valid, y: base history weight,
    // zw: internal-resolution / display-resolution scale.
    float4 taaParams;
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

PSOutput PSMain(VSOutput input)
{
    const float2 uv = input.uv;
    uint internalWidth;
    uint internalHeight;
    sceneColor.GetDimensions(internalWidth, internalHeight);
    const float2 internalSize = float2(internalWidth, internalHeight);
    const bool isUpscaling = any(taaParams.zw < 0.999f.xx);
    const bool isDownscaling = any(taaParams.zw > 1.001f.xx);
    const float3 currentColor = isUpscaling
        ? SampleCatmullRom(sceneColor, sceneColorSampler, uv, internalSize)
        : isDownscaling
            ? SampleSupersampled(
                sceneColor, sceneColorSampler, uv, internalSize, taaParams.zw)
            : sceneColor.SampleLevel(sceneColorSampler, uv, 0.0f).rgb;
    const float currentDepth = sceneDepth.SampleLevel(
        sceneDepthSampler, uv, 0.0f).r;
    const float3 currentEncodedNormal = gBufferNormal.SampleLevel(
        gBufferNormalSampler, uv, 0.0f).xyz;
    if (taaParams.x < 0.5f) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }
    if (currentDepth >= 0.999999f) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    // Dilate foreground velocity into geometric edges by choosing the closest
    // depth in a 3x3 neighborhood. Bilinear velocity at silhouettes mixes
    // unrelated foreground/background motion and is a major source of trails.
    float closestDepth = currentDepth;
    float2 closestUv = uv;
    const float2 internalTexelSize = rcp(internalSize);
    [unroll]
    for (int velocityY = -1; velocityY <= 1; ++velocityY) {
        [unroll]
        for (int velocityX = -1; velocityX <= 1; ++velocityX) {
            const float2 candidateUv = uv +
                float2(velocityX, velocityY) * internalTexelSize;
            const float candidateDepth = sceneDepth.SampleLevel(
                sceneDepthSampler, candidateUv, 0.0f).r;
            if (candidateDepth < closestDepth) {
                closestDepth = candidateDepth;
                closestUv = candidateUv;
            }
        }
    }
    const float2 velocity = gBufferVelocity.SampleLevel(
        gBufferVelocitySampler, closestUv, 0.0f).xy;
    const float2 historyUv = uv - velocity;
    if (any(historyUv <= 0.0f.xx) || any(historyUv >= 1.0f.xx)) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    const float4 historyData = taaHistoryData.SampleLevel(
        taaHistoryDataSampler, historyUv, 0.0f);
    const float historyDepth = historyData.w;
    const float depthThreshold = max(0.0015f, abs(closestDepth) * 0.01f);
    if (abs(closestDepth - historyDepth) > depthThreshold) {
        return MakeTemporalOutput(
            currentColor, currentEncodedNormal, currentDepth);
    }

    const float3 currentNormal = normalize(
        currentEncodedNormal * 2.0f - 1.0f);
    const float3 historyNormal = normalize(
        historyData.xyz * 2.0f - 1.0f);
    if (dot(currentNormal, historyNormal) < 0.75f) {
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
                sceneColorSampler, uv, 0.0f, int2(x, y)).rgb);
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
    const float3 varianceMin = neighborhoodMean - 1.75f * neighborhoodSigma;
    const float3 varianceMax = neighborhoodMean + 1.75f * neighborhoodSigma;
    const float3 neighborhoodExtent =
        max(neighborhoodMax - neighborhoodMin, 0.001f.xxx);
    neighborhoodMin = max(
        neighborhoodMin - neighborhoodExtent * 0.05f,
        varianceMin);
    neighborhoodMax = min(
        neighborhoodMax + neighborhoodExtent * 0.05f,
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
    historyColor = YCoCgToRGB(clamp(
        RGBToYCoCg(historyColor), neighborhoodMin, neighborhoodMax));

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
    const float reactiveMask = gBufferMaterial.SampleLevel(
        gBufferMaterialSampler, uv, 0.0f).a > (0.5f / 255.0f)
        ? 1.0f
        : 0.0f;
    const float historyWeight =
        saturate(taaParams.y) * motionConfidence *
        luminanceConfidence * (1.0f - reactiveMask);
    return MakeTemporalOutput(
        lerp(currentColor, historyColor, historyWeight),
        currentEncodedNormal,
        currentDepth);
}
