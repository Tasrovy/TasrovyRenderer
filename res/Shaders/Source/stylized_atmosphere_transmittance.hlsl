struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

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

float RayleighDensity(float heightKm)
{
    return exp(-max(heightKm, 0.0f) / 8.0f);
}

float MieDensity(float heightKm)
{
    return exp(-max(heightKm, 0.0f) / 1.2f);
}

float OzoneDensity(float heightKm)
{
    return saturate(1.0f - abs(heightKm - 25.0f) / 15.0f);
}

float4 PSMain(VSOutput input) : SV_Target0
{
    static const float bottomRadiusKm = 6360.0f;
    static const float topRadiusKm = 6460.0f;
    static const float3 rayleighExtinction =
        float3(0.005802f, 0.013558f, 0.033100f);
    static const float3 mieExtinction = 0.004440f.xxx;
    static const float3 ozoneExtinction =
        float3(0.000650f, 0.001881f, 0.000085f);

    // Reconstruct the physical (r, mu) domain from the non-linear LUT
    // parameterization used by the captured pass. V maps to tangent distance;
    // U interpolates between the shortest and longest valid path to the top.
    const float atmosphereHorizon = sqrt(
        topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm);
    const float tangentDistance = atmosphereHorizon * saturate(input.uv.y);
    const float r = sqrt(
        tangentDistance * tangentDistance +
        bottomRadiusKm * bottomRadiusKm);
    const float distanceToTop = topRadiusKm - r;
    const float mappedDistance = max(
        lerp(
            distanceToTop,
            tangentDistance + atmosphereHorizon,
            saturate(input.uv.x)),
        1.0e-3f);
    const float mu = clamp(
        (atmosphereHorizon * atmosphereHorizon -
            tangentDistance * tangentDistance -
            mappedDistance * mappedDistance) /
            (2.0f * r * mappedDistance),
        -1.0f,
        1.0f);
    const float discriminant = max(
        r * r * (mu * mu - 1.0f) + topRadiusKm * topRadiusKm,
        0.0f);
    const float rayLength = max(0.0f, -r * mu + sqrt(discriminant));

    static const uint sampleCount = 32u;
    const float stepSize = rayLength / (float)sampleCount;
    float3 opticalDepth = 0.0f.xxx;
    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex) {
        const float t = ((float)sampleIndex + 0.5f) * stepSize;
        const float sampleRadius = sqrt(max(
            r * r + t * t + 2.0f * r * mu * t,
            bottomRadiusKm * bottomRadiusKm));
        const float heightKm = max(sampleRadius - bottomRadiusKm, 0.0f);
        const float3 extinction =
            rayleighExtinction * RayleighDensity(heightKm) +
            mieExtinction * MieDensity(heightKm) +
            ozoneExtinction * OzoneDensity(heightKm);
        opticalDepth += extinction * stepSize;
    }

    return float4(exp(-opticalDepth), 1.0f);
}
