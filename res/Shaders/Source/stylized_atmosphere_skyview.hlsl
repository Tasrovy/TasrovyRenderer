[[vk::combinedImageSampler]] Texture2D transmittanceLut : register(t0, space0);
[[vk::combinedImageSampler]] SamplerState transmittanceSampler : register(s0, space0);
[[vk::combinedImageSampler]] Texture2D multiScatteringLut : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState multiScatteringSampler : register(s1, space0);

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

static const float bottomRadiusKm = 6360.0f;
static const float topRadiusKm = 6460.0f;
static const float3 rayleighScattering =
    float3(0.005802f, 0.013558f, 0.033100f);
static const float3 mieScattering = 0.003996f.xxx;
static const float3 mieExtinction = 0.004440f.xxx;
static const float3 ozoneExtinction =
    float3(0.000650f, 0.001881f, 0.000085f);

float RayleighDensity(float h) { return exp(-max(h, 0.0f) / 8.0f); }
float MieDensity(float h) { return exp(-max(h, 0.0f) / 1.2f); }
float OzoneDensity(float h) {
    return saturate(1.0f - abs(h - 25.0f) / 15.0f);
}

float2 TransmittanceUv(float r, float mu)
{
    const float horizon = sqrt(
        topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm);
    const float rho = sqrt(max(
        r * r - bottomRadiusKm * bottomRadiusKm, 0.0f));
    const float distance = max(
        -r * mu + sqrt(max(
            r * r * (mu * mu - 1.0f) + topRadiusKm * topRadiusKm,
            0.0f)),
        0.0f);
    const float minimumDistance = topRadiusKm - r;
    const float maximumDistance = rho + horizon;
    return saturate(float2(
        (distance - minimumDistance) /
            max(maximumDistance - minimumDistance, 1.0e-4f),
        rho / horizon));
}

float3 SampleSunTransmittance(float r, float muSun)
{
    const float groundCosine = -sqrt(saturate(
        1.0f - bottomRadiusKm * bottomRadiusKm / (r * r)));
    if (muSun < groundCosine) return 0.0f.xxx;
    return transmittanceLut.SampleLevel(
        transmittanceSampler, TransmittanceUv(r, muSun), 0.0f).rgb;
}

float RayleighPhase(float cosineTheta)
{
    return 0.05968310366f * (1.0f + cosineTheta * cosineTheta);
}

float MiePhase(float cosineTheta)
{
    static const float g = 0.8f;
    const float denominator = max(
        1.0f + g * g - 2.0f * g * cosineTheta, 1.0e-3f);
    return 0.07957747155f * (1.0f - g * g) /
        (denominator * sqrt(denominator));
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float azimuth = (saturate(input.uv.x) * 2.0f - 1.0f) *
        3.14159265359f;
    const float viewZenith = saturate(input.uv.y) * 3.14159265359f;
    const float3 viewDirection = normalize(float3(
        sin(viewZenith) * cos(azimuth),
        cos(viewZenith),
        sin(viewZenith) * sin(azimuth)));
    const float sunZenith = 0.85f;
    const float3 sunDirection = normalize(float3(
        sin(sunZenith), cos(sunZenith), 0.0f));

    const float cameraRadius = bottomRadiusKm + 0.2f;
    const float3 origin = float3(0.0f, cameraRadius, 0.0f);
    const float muView = viewDirection.y;
    const float topDistance = max(
        -cameraRadius * muView + sqrt(max(
            cameraRadius * cameraRadius * (muView * muView - 1.0f) +
                topRadiusKm * topRadiusKm,
            0.0f)),
        0.0f);
    const float groundDiscriminant =
        cameraRadius * cameraRadius * (muView * muView - 1.0f) +
        bottomRadiusKm * bottomRadiusKm;
    float rayLength = topDistance;
    bool hitsGround = false;
    if (muView < 0.0f && groundDiscriminant >= 0.0f) {
        const float groundDistance =
            -cameraRadius * muView - sqrt(groundDiscriminant);
        if (groundDistance > 0.0f && groundDistance < rayLength) {
            rayLength = groundDistance;
            hitsGround = true;
        }
    }

    static const uint sampleCount = 32u;
    const float stepSize = rayLength / (float)sampleCount;
    const float viewSunCosine = dot(viewDirection, sunDirection);
    float3 throughput = 1.0f.xxx;
    float3 radiance = 0.0f.xxx;
    [loop]
    for (uint i = 0u; i < sampleCount; ++i) {
        const float t = ((float)i + 0.5f) * stepSize;
        const float3 samplePosition = origin + viewDirection * t;
        const float sampleRadius = length(samplePosition);
        const float3 radial = samplePosition / max(sampleRadius, 1.0e-4f);
        const float muSun = dot(radial, sunDirection);
        const float height = max(sampleRadius - bottomRadiusKm, 0.0f);
        const float rayleigh = RayleighDensity(height);
        const float mie = MieDensity(height);
        const float3 scattering =
            rayleighScattering * rayleigh + mieScattering * mie;
        const float3 extinction = rayleighScattering * rayleigh +
            mieExtinction * mie + ozoneExtinction * OzoneDensity(height);
        const float3 singleScattering = SampleSunTransmittance(
            sampleRadius, muSun) *
            (rayleighScattering * rayleigh * RayleighPhase(viewSunCosine) +
             mieScattering * mie * MiePhase(viewSunCosine));
        const float2 msUv = saturate(float2(
            muSun * 0.5f + 0.5f,
            (sampleRadius - bottomRadiusKm) /
                (topRadiusKm - bottomRadiusKm)));
        const float3 multipleScattering = multiScatteringLut.SampleLevel(
            multiScatteringSampler, msUv, 0.0f).rgb * scattering;
        radiance += throughput *
            (singleScattering + multipleScattering) * stepSize;
        throughput *= exp(-extinction * stepSize);
    }

    if (hitsGround) {
        const float3 groundPoint = origin + viewDirection * rayLength;
        const float3 groundNormal = normalize(groundPoint);
        const float groundMuSun = dot(groundNormal, sunDirection);
        radiance += throughput * max(groundMuSun, 0.0f) *
            SampleSunTransmittance(
                bottomRadiusKm + 0.01f, groundMuSun) *
            0.3f / 3.14159265359f;
    }
    return float4(radiance, 1.0f);
}
