[[vk::combinedImageSampler]] Texture2D transmittanceLut : register(t0, space0);
[[vk::combinedImageSampler]] SamplerState transmittanceSampler : register(s0, space0);

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
static const float isotropicPhase = 0.07957747155f;

float RayleighDensity(float h) { return exp(-max(h, 0.0f) / 8.0f); }
float MieDensity(float h) { return exp(-max(h, 0.0f) / 1.2f); }
float OzoneDensity(float h) {
    return saturate(1.0f - abs(h - 25.0f) / 15.0f);
}

void MediumAtRadius(
    float radius,
    out float3 scattering,
    out float3 extinction)
{
    const float height = max(radius - bottomRadiusKm, 0.0f);
    const float rayleigh = RayleighDensity(height);
    const float mie = MieDensity(height);
    scattering = rayleighScattering * rayleigh + mieScattering * mie;
    extinction = rayleighScattering * rayleigh +
        mieExtinction * mie + ozoneExtinction * OzoneDensity(height);
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

float3 TransmittanceToSun(float r, float muSun)
{
    const float groundCosine = -sqrt(saturate(
        1.0f - bottomRadiusKm * bottomRadiusKm / (r * r)));
    if (muSun < groundCosine) return 0.0f.xxx;
    return transmittanceLut.SampleLevel(
        transmittanceSampler, TransmittanceUv(r, muSun), 0.0f).rgb;
}

struct MarchResult
{
    float3 radiance;
    float3 scatteringRatio;
    float3 throughput;
};

MarchResult MarchRadial(float r, float muSun, float direction)
{
    MarchResult result;
    result.radiance = 0.0f.xxx;
    result.scatteringRatio = 0.0f.xxx;
    result.throughput = 1.0f.xxx;

    const float rayLength = direction > 0.0f
        ? topRadiusKm - r
        : max(r - bottomRadiusKm, 0.0f);
    static const uint sampleCount = 24u;
    const float stepSize = rayLength / (float)sampleCount;
    [loop]
    for (uint i = 0u; i < sampleCount; ++i) {
        const float t = ((float)i + 0.5f) * stepSize;
        const float sampleRadius = clamp(
            r + direction * t, bottomRadiusKm, topRadiusKm);
        float3 scattering;
        float3 extinction;
        MediumAtRadius(sampleRadius, scattering, extinction);
        const float3 segmentTransmittance = exp(-extinction * stepSize);
        const float3 source =
            TransmittanceToSun(sampleRadius, muSun) *
            scattering * isotropicPhase;
        result.radiance += result.throughput * source * stepSize;
        result.scatteringRatio +=
            result.throughput * scattering * isotropicPhase * stepSize;
        result.throughput *= segmentTransmittance;
    }
    return result;
}

float4 PSMain(VSOutput input) : SV_Target0
{
    const float r = lerp(
        bottomRadiusKm + 0.01f,
        topRadiusKm - 0.01f,
        saturate(input.uv.y));
    const float muSun = lerp(-1.0f, 1.0f, saturate(input.uv.x));
    const MarchResult up = MarchRadial(r, muSun, 1.0f);
    const MarchResult down = MarchRadial(r, muSun, -1.0f);

    const float3 sunAtGround = TransmittanceToSun(
        bottomRadiusKm + 0.01f, muSun);
    const float3 groundBounce = down.throughput *
        max(muSun, 0.0f) * sunAtGround * 0.3f / 3.14159265359f;

    const float3 f = saturate(
        (up.scatteringRatio + down.scatteringRatio) * 0.5f);
    const float3 f2 = f * f;
    const float3 higherOrders =
        1.0f.xxx + f + f2 + f2 * f + f2 * f2;
    const float3 multiScattering =
        ((up.radiance + down.radiance + groundBounce) * 0.5f) *
        higherOrders;
    return float4(multiScattering, 1.0f);
}
