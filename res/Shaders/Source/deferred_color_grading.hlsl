cbuffer FinalDisplayConstants : register(b0, space0)
{
    // x LUT enabled, y LUT strength, z exposure, w debug-output bypass.
    float4 displayParameters;
    // xy inverse display extent, z aspect ratio, w CAS strength.
    float4 resolutionAndSharpening;
    // x chromatic aberration pixels, y vignette strength,
    // z vignette power, w display-encoded dither strength.
    float4 lensAndOutput;
    // xy vignette center, z radius, w reserved.
    float4 vignetteGeometry;
    // rgb vignette tint, w Bloom enabled.
    float4 vignetteColorAndBloom;
    // x Bloom intensity, y LUT input exposure compensation in EV.
    float4 bloomParameters;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D postProcessedHdrColor
    : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState postProcessedHdrColorSampler
    : register(s1, space0);
[[vk::combinedImageSampler]] Texture2D bloomLowRes
    : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState bloomLowResSampler
    : register(s2, space0);
[[vk::combinedImageSampler]] Texture2D colorGradingLut
    : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState colorGradingLutSampler
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

float3 SampleScene(float2 uv)
{
    return postProcessedHdrColor.SampleLevel(
        postProcessedHdrColorSampler, uv, 0.0f).rgb;
}

float3 ApplyRadialChromaticAberration(float2 uv, float strengthPixels)
{
    const float3 center = SampleScene(uv);
    if (strengthPixels <= 0.0f) {
        return center;
    }

    float2 radial = uv - 0.5f.xx;
    const float radiusSquared = dot(radial, radial);
    radial *= sqrt(max(radiusSquared, 0.0f));
    const float2 offset =
        radial * resolutionAndSharpening.xy * strengthPixels;
    const float red = SampleScene(uv - offset).r;
    const float green = center.g;
    const float blue = SampleScene(uv + offset).b;
    return float3(red, green, blue);
}

float LumaForSharpening(float3 color)
{
    // Pass 29 uses 0.5R + G + 0.5B for its local contrast estimate.
    return dot(color, float3(0.5f, 1.0f, 0.5f));
}

float3 ApplyContrastAdaptiveSharpening(
    float2 uv,
    float3 center,
    float strength)
{
    if (strength <= 0.0f) {
        return center;
    }

    const float2 texel = resolutionAndSharpening.xy;
    const float3 up = SampleScene(uv - float2(0.0f, texel.y));
    const float3 left = SampleScene(uv - float2(texel.x, 0.0f));
    const float3 right = SampleScene(uv + float2(texel.x, 0.0f));
    const float3 down = SampleScene(uv + float2(0.0f, texel.y));

    const float centerLuma = LumaForSharpening(center);
    const float4 neighborLuma = float4(
        LumaForSharpening(up),
        LumaForSharpening(left),
        LumaForSharpening(right),
        LumaForSharpening(down));
    const float neighborAverage = dot(neighborLuma, 0.25f.xxxx);
    const float lumaMinimum = min(centerLuma, min(
        min(neighborLuma.x, neighborLuma.y),
        min(neighborLuma.z, neighborLuma.w)));
    const float lumaMaximum = max(centerLuma, max(
        max(neighborLuma.x, neighborLuma.y),
        max(neighborLuma.z, neighborLuma.w)));
    const float edge = saturate(
        abs(neighborAverage - centerLuma) /
        max(lumaMaximum - lumaMinimum, 1.0e-5f));

    const float3 minimum4 = min(min(up, left), min(right, down));
    const float3 maximum4 = max(max(up, left), max(right, down));
    const float3 darkLimit =
        -minimum4 / (4.0f * max(maximum4, 1.0e-4f.xxx));
    const float3 lightLimit =
        (1.0f.xxx - maximum4) /
        min(4.0f * minimum4 - 4.0f.xxx, -1.0e-4f.xxx);
    const float3 channelWeights = max(darkLimit, lightLimit);
    float peak = max(
        channelWeights.r,
        max(channelWeights.g, channelWeights.b));
    peak = clamp(peak, -0.1875f, 0.0f);

    const float weight =
        peak * strength * (1.0f - 0.5f * edge);
    return max(
        (center + weight * (up + left + right + down)) /
            max(1.0f + 4.0f * weight, 1.0e-4f),
        0.0f.xxx);
}

float3 ApplyVignette(float3 color, float2 uv)
{
    const float strength = saturate(lensAndOutput.y);
    if (strength <= 0.0f) {
        return color;
    }

    float2 centered = (uv - vignetteGeometry.xy) * 2.0f;
    centered.x *= resolutionAndSharpening.z;
    const float normalizedRadius = length(centered) /
        max(vignetteGeometry.z, 1.0e-4f);
    const float mask = pow(
        saturate(normalizedRadius),
        max(lensAndOutput.z, 0.01f));
    const float3 tint = lerp(
        1.0f.xxx,
        vignetteColorAndBloom.rgb,
        saturate(mask * strength));
    return color * tint;
}

float3 EncodeLogC(float3 sceneLinear)
{
    const float3 encoded =
        log2(max(sceneLinear * 5.555556f + 0.047996f, 1.0e-6f.xxx))
        * 0.301030f * 0.244161f
        + 0.386036f;
    return saturate(encoded);
}

float3 SampleFlattened3DLut(float3 logColor)
{
    uint lutWidth;
    uint lutHeight;
    colorGradingLut.GetDimensions(lutWidth, lutHeight);

    const float lutSize = (float)lutHeight;
    const float blueSlice = logColor.b * (lutSize - 1.0f);
    const float slice0 = floor(blueSlice);
    const float slice1 = min(slice0 + 1.0f, lutSize - 1.0f);
    const float sliceBlend = frac(blueSlice);
    const float2 texelInSlice =
        logColor.rg * (lutSize - 1.0f) + 0.5f;

    const float2 uv0 = float2(
        slice0 * lutSize + texelInSlice.x,
        texelInSlice.y) / float2((float)lutWidth, (float)lutHeight);
    const float2 uv1 = float2(
        slice1 * lutSize + texelInSlice.x,
        texelInSlice.y) / float2((float)lutWidth, (float)lutHeight);

    const float3 graded0 = colorGradingLut.SampleLevel(
        colorGradingLutSampler, uv0, 0.0f).rgb;
    const float3 graded1 = colorGradingLut.SampleLevel(
        colorGradingLutSampler, uv1, 0.0f).rgb;
    return lerp(graded0, graded1, sliceBlend);
}

float3 LinearToSrgb(float3 linearColor)
{
    linearColor = max(linearColor, 0.0f.xxx);
    const float3 low = linearColor * 12.92f;
    const float3 high =
        1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, linearColor <= 0.0031308f.xxx);
}

float3 SrgbToLinear(float3 encodedColor)
{
    encodedColor = saturate(encodedColor);
    const float3 low = encodedColor / 12.92f;
    const float3 high = pow(
        (encodedColor + 0.055f) / 1.055f,
        2.4f);
    return lerp(high, low, encodedColor <= 0.04045f.xxx);
}

float3 ApplyDisplayEncodedDither(float3 linearColor, float2 uv)
{
    const float strength = max(lensAndOutput.w, 0.0f);
    if (strength <= 0.0f) {
        return linearColor;
    }

    const float2 pixelPosition = uv / max(
        resolutionAndSharpening.xy,
        1.0e-6f.xx);
    const float seed = dot(float2(171.0f, 231.0f), pixelPosition);
    const float3 noise =
        frac(seed * float3(0.0097f, 0.0141f, 0.0103f)) - 0.5f.xxx;
    const float3 encoded =
        LinearToSrgb(linearColor) + noise * (strength / 255.0f);
    // The swapchain is sRGB. Convert the dithered encoded value back to
    // linear so the attachment conversion produces the intended bits.
    return SrgbToLinear(encoded);
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float4 source = postProcessedHdrColor.SampleLevel(
        postProcessedHdrColorSampler, input.uv, 0.0f);
    if (displayParameters.w > 0.5f) {
        return source;
    }

    float3 color = ApplyRadialChromaticAberration(
        input.uv,
        lensAndOutput.x);
    color = ApplyContrastAdaptiveSharpening(
        input.uv,
        color,
        resolutionAndSharpening.w);

    if (vignetteColorAndBloom.w > 0.5f) {
        color += bloomLowRes.SampleLevel(
            bloomLowResSampler,
            input.uv,
            0.0f).rgb * max(bloomParameters.x, 0.0f);
    }
    color = ApplyVignette(color, input.uv);

    const float exposure = max(displayParameters.z, 0.0001f) *
        exp2(bloomParameters.y);
    const float3 exposedHdr = max(color, 0.0f.xxx) * exposure;
    const float3 fallback = 1.0f.xxx - exp(-exposedHdr);
    const float3 graded = SampleFlattened3DLut(EncodeLogC(exposedHdr));
    const float lutStrength = saturate(
        displayParameters.x * displayParameters.y);
    float3 displayLinear = lerp(fallback, graded, lutStrength);
    displayLinear = ApplyDisplayEncodedDither(displayLinear, input.uv);
    return float4(displayLinear, source.a);
}
