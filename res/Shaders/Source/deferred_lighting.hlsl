struct GpuLightData
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 parameters;
};

cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
    float4 lightColor;
    float4 camPosAndMetallic;
    float4 roughnessAo;
    float4 uvTransform;
    float4 baseColorFactorAndTexture;
    float4 materialEmission;
    float4 materialRimColorAndStrength;
    float4 materialRimParams;
    float4 lightMeta;
    GpuLightData lights[8];
    matrix lightViewProj;
    float4 shadowParams;
    float4 advancedLightingParams;
    float4 pcssParams;
    float4 ssaoParams;
    float4 ssdoParams;
    float4 ssrParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D shadowMap : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState shadowSampler : register(s1, space0);

[[vk::combinedImageSampler]] Texture2D gBufferAlbedo : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState gBufferAlbedoSampler : register(s2, space0);

[[vk::combinedImageSampler]] Texture2D gBufferNormal : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState gBufferNormalSampler : register(s3, space0);

[[vk::combinedImageSampler]] Texture2D gBufferMaterial : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState gBufferMaterialSampler : register(s4, space0);

[[vk::combinedImageSampler]] Texture2D gBufferEffects : register(t5, space0);
[[vk::combinedImageSampler]] SamplerState gBufferEffectsSampler : register(s5, space0);

[[vk::combinedImageSampler]] Texture2D gBufferWorldPos : register(t6, space0);
[[vk::combinedImageSampler]] SamplerState gBufferWorldPosSampler : register(s6, space0);

[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t7, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s7, space0);

[[vk::combinedImageSampler]] TextureCube irradianceMap : register(t8, space0);
[[vk::combinedImageSampler]] SamplerState irradianceSampler : register(s8, space0);

[[vk::combinedImageSampler]] TextureCube prefilteredMap : register(t9, space0);
[[vk::combinedImageSampler]] SamplerState prefilteredSampler : register(s9, space0);

[[vk::combinedImageSampler]] Texture2D brdfLUT : register(t10, space0);
[[vk::combinedImageSampler]] SamplerState brdfSampler : register(s10, space0);

[[vk::combinedImageSampler]] Texture2D hbaoTexture : register(t11, space0);
[[vk::combinedImageSampler]] SamplerState hbaoSampler : register(s11, space0);

#define PI 3.14159265359f

static const uint ShadingModel_DefaultLit = 0;
static const uint ShadingModel_Unlit = 1;

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

float3 DecodeNormal(float3 encodedNormal)
{
    return normalize(encodedNormal * 2.0f - 1.0f);
}

uint DecodeShadingModel(float encodedValue)
{
    return (uint)round(saturate(encodedValue) * 255.0f);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 0.001f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 0.001f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

static const float2 PoissonDisk[16] = {
    float2(-0.94201624f, -0.39906216f), float2(0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2(0.34495938f, 0.29387760f),
    float2(-0.91588581f, 0.45771432f), float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f, 0.27676845f), float2(0.97484398f, 0.75648379f),
    float2(0.44323325f, -0.97511554f), float2(0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2(0.79197514f, 0.19090188f),
    float2(-0.24188840f, 0.99706507f), float2(-0.81409955f, 0.91437590f),
    float2(0.19984126f, 0.78641367f), float2(0.14383161f, -0.14100790f)
};

static const float2 ScreenDirections[8] = {
    float2(1.0f, 0.0f), float2(-1.0f, 0.0f),
    float2(0.0f, 1.0f), float2(0.0f, -1.0f),
    float2(0.7071f, 0.7071f), float2(-0.7071f, 0.7071f),
    float2(0.7071f, -0.7071f), float2(-0.7071f, -0.7071f)
};

float CalculateShadow(float3 worldPos, float3 normal, float3 lightDirection)
{
    float4 lightClip = mul(float4(worldPos, 1.0f), lightViewProj);
    if (lightClip.w <= 0.0f) {
        return 0.0f;
    }

    float3 projected = lightClip.xyz / lightClip.w;
    float2 shadowUv = projected.xy * 0.5f + 0.5f;
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
        shadowUv.y < 0.0f || shadowUv.y > 1.0f ||
        projected.z < 0.0f || projected.z > 1.0f) {
        return 0.0f;
    }

    uint shadowWidth;
    uint shadowHeight;
    shadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = rcp(float2(shadowWidth, shadowHeight));
    float bias = max(
        shadowParams.x * (1.0f - saturate(dot(normal, lightDirection))),
        shadowParams.y);
    float receiverDepth = projected.z - bias;

    // Classify the local shadow footprint first. Most pixels are fully lit or
    // fully shadowed and can return after one hardware gather; only boundaries
    // need an expensive soft-shadow search.
    float4 coarseDepths = shadowMap.GatherRed(shadowSampler, shadowUv);
    float4 coarseShadow = step(coarseDepths, receiverDepth.xxxx);
    float coarseCoverage = dot(coarseShadow, 0.25f.xxxx);
    const bool shadowEdge = coarseCoverage > 0.001f && coarseCoverage < 0.999f;

    if (advancedLightingParams.x > 0.5f) {
        if (!shadowEdge) {
            return coarseCoverage;
        }

        float searchRadius = max(pcssParams.x * saturate(projected.z), texelSize.x);
        float blockerDepth = 0.0f;
        float blockerCount = 0.0f;
        // Close receivers and strongly mixed edge footprints receive more
        // blocker samples. Distant receivers use four samples.
        const float edgeStrength = 1.0f - abs(coarseCoverage * 2.0f - 1.0f);
        const int blockerSampleCount = clamp(
            (int)round(8.0f - saturate(projected.z) * 4.0f + edgeStrength * 2.0f),
            4,
            8);
        [loop]
        for (int blockerIndex = 0; blockerIndex < blockerSampleCount; ++blockerIndex) {
            float2 sampleUv = clamp(
                shadowUv + PoissonDisk[blockerIndex] * searchRadius,
                texelSize * 0.5f,
                1.0f - texelSize * 0.5f);
            float sampleDepth = shadowMap.SampleLevel(shadowSampler, sampleUv, 0.0f).r;
            if (sampleDepth < receiverDepth) {
                blockerDepth += sampleDepth;
                blockerCount += 1.0f;
            }
        }
        if (blockerCount < 0.5f) {
            return 0.0f;
        }

        blockerDepth /= blockerCount;
        float penumbra = (receiverDepth - blockerDepth) / max(blockerDepth, 0.0001f);
        float filterRadius = clamp(
            penumbra * pcssParams.x,
            max(texelSize.x, texelSize.y),
            max(pcssParams.y, max(texelSize.x, texelSize.y)));
        float shadow = 0.0f;
        const float normalizedPenumbra = saturate(
            filterRadius / max(pcssParams.y, max(texelSize.x, texelSize.y)));
        const int filterSampleCount = clamp(
            (int)round(6.0f + edgeStrength * 2.0f + normalizedPenumbra * 4.0f),
            6,
            12);
        [loop]
        for (int filterIndex = 0; filterIndex < filterSampleCount; ++filterIndex) {
            float2 sampleUv = clamp(
                shadowUv + PoissonDisk[filterIndex] * filterRadius,
                texelSize * 0.5f,
                1.0f - texelSize * 0.5f);
            float sampleDepth = shadowMap.SampleLevel(shadowSampler, sampleUv, 0.0f).r;
            shadow += receiverDepth > sampleDepth ? 1.0f : 0.0f;
        }
        return shadow / (float)filterSampleCount;
    }

    return coarseCoverage;
}

float3 CalculateSSDO(float2 uv, float3 worldPos, float3 normal)
{
    if (advancedLightingParams.z < 0.5f) {
        return 0.0f.xxx;
    }

    uint width;
    uint height;
    sceneDepth.GetDimensions(width, height);
    float2 texelSize = rcp(float2(width, height));
    float3 indirect = 0.0f.xxx;
    float totalWeight = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 sampleUv = saturate(
            uv + ScreenDirections[sampleIndex] * texelSize * ssdoParams.x);
        float sampleDepth = sceneDepth.SampleLevel(sceneDepthSampler, sampleUv, 0.0f).r;
        if (sampleDepth >= 0.99999f) {
            continue;
        }
        float3 samplePosition =
            gBufferWorldPos.SampleLevel(gBufferWorldPosSampler, sampleUv, 0.0f).xyz;
        float3 delta = samplePosition - worldPos;
        float distanceToSample = length(delta);
        if (distanceToSample < 0.0001f || distanceToSample > ssdoParams.z) {
            continue;
        }
        float3 direction = delta / distanceToSample;
        float3 sampleNormal = DecodeNormal(
            gBufferNormal.SampleLevel(gBufferNormalSampler, sampleUv, 0.0f).rgb);
        float geometry = saturate(dot(normal, direction) - ssdoParams.w) *
            saturate(dot(sampleNormal, -direction));
        float rangeWeight = 1.0f - saturate(distanceToSample / ssdoParams.z);
        float weight = geometry * rangeWeight;
        float3 sampleAlbedo =
            gBufferAlbedo.SampleLevel(gBufferAlbedoSampler, sampleUv, 0.0f).rgb;
        indirect += sampleAlbedo * weight;
        totalWeight += weight;
    }
    return totalWeight > 0.0001f
        ? indirect / totalWeight * ssdoParams.y
        : 0.0f.xxx;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float sceneDepthValue = sceneDepth.SampleLevel(sceneDepthSampler, input.uv, 0.0f).r;
    if (sceneDepthValue >= 0.99999f) {
        return 0.0f.xxxx;
    }
    float3 albedo = gBufferAlbedo.Sample(gBufferAlbedoSampler, input.uv).rgb;
    float3 normal = DecodeNormal(gBufferNormal.Sample(gBufferNormalSampler, input.uv).rgb);
    float4 material = gBufferMaterial.Sample(gBufferMaterialSampler, input.uv);
    float4 worldPositionAndRimPower =
        gBufferWorldPos.Sample(gBufferWorldPosSampler, input.uv);
    float3 worldPos = worldPositionAndRimPower.xyz;
    float4 rimEffects = gBufferEffects.Sample(gBufferEffectsSampler, input.uv);

    uint debugMode = (uint)round(roughnessAo.z);
    if (debugMode > 0) {
        return float4(albedo, 1.0f);
    }

    float metallic = saturate(material.r);
    float roughness = max(saturate(material.g), 0.04f);
    float ao = saturate(material.b);
    uint shadingModel = DecodeShadingModel(material.a);

    if (shadingModel == ShadingModel_Unlit) {
        return float4(albedo, 1.0f);
    }

    float3 V = normalize(camPosAndMetallic.xyz - worldPos);
    float3 R = reflect(-V, normal);

    float NdotV = max(dot(normal, V), 0.0f);

    float3 F0 = lerp(0.04f.xxx, albedo, metallic);
    float3 direct = 0.0f.xxx;
    uint lightCount = min((uint)round(lightMeta.x), 8u);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
        GpuLightData light = lights[lightIndex];
        uint lightType = (uint)round(light.positionAndType.w);
        float3 L = 0.0f.xxx;
        float attenuation = 1.0f;

        if (lightType == 0u) {
            L = normalize(-light.directionAndRange.xyz);
        } else {
            float3 toLight = light.positionAndType.xyz - worldPos;
            float distanceToLight = max(length(toLight), 0.001f);
            L = toLight / distanceToLight;
            if (lightType == 1u) {
                float constantAttenuation = max(light.parameters.x, 0.0001f);
                attenuation = rcp(max(
                    constantAttenuation +
                    light.parameters.y * distanceToLight +
                    light.parameters.z * distanceToLight * distanceToLight,
                    0.0001f));
            } else {
                float area = max(light.parameters.x * light.parameters.y, 0.0001f);
                float emitterFacing = dot(
                    normalize(light.directionAndRange.xyz),
                    normalize(worldPos - light.positionAndType.xyz));
                if (light.parameters.z > 0.5f) {
                    emitterFacing = abs(emitterFacing);
                } else {
                    emitterFacing = saturate(emitterFacing);
                }
                attenuation = emitterFacing * area / (distanceToLight * distanceToLight + area);
            }
        }

        float NdotL = saturate(dot(normal, L));
        if (NdotL <= 0.0f || attenuation <= 0.0f) {
            continue;
        }

        float3 H = normalize(L + V);
        float D = DistributionGGX(normal, H, roughness);
        float G = GeometrySmith(normal, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
        float3 numerator = D * G * F;
        float denominator = 4.0f * max(NdotV, 0.001f) * max(NdotL, 0.001f);
        float3 specularDirect = numerator / max(denominator, 0.001f);
        float3 kDDirect = (1.0f.xxx - F) * (1.0f - metallic);
        float3 diffuseDirect = kDDirect * albedo / PI;
        float3 radiance =
            light.colorAndIntensity.rgb * light.colorAndIntensity.w * attenuation * NdotL;
        if (abs((float)lightIndex - shadowParams.z) < 0.5f) {
            float shadow = CalculateShadow(worldPos, normal, L);
            radiance *= 1.0f - shadow * saturate(shadowParams.w);
        }
        direct += (diffuseDirect + specularDirect) * radiance;
    }

    const float MAX_REFLECTION_LOD = 7.0f;
    float3 F = FresnelSchlick(NdotV, F0);
    float3 kD = (1.0f.xxx - F) * (1.0f - metallic);
    float3 irradiance = irradianceMap.Sample(irradianceSampler, normal).rgb;
    float3 diffuseIBL = kD * irradiance * albedo;

    float3 prefilteredColor =
        prefilteredMap.SampleLevel(prefilteredSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = brdfLUT.Sample(brdfSampler, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    float screenAo = advancedLightingParams.y > 0.5f
        ? hbaoTexture.SampleLevel(hbaoSampler, input.uv, 0.0f).r
        : 1.0f;
    float3 screenIndirect = CalculateSSDO(input.uv, worldPos, normal) * albedo;
    float3 color =
        (diffuseIBL + specularIBL) * ao * screenAo * lightMeta.y +
        direct * lerp(1.0f, screenAo, 0.35f) +
        screenIndirect;
    float rim = pow(
        saturate(1.0f - NdotV),
        max(worldPositionAndRimPower.w, 0.25f)) * max(rimEffects.a, 0.0f);
    color += rimEffects.rgb * rim;
    // Preserve HDR values for bloom extraction and tone mapping.
    return float4(max(color, 0.0f.xxx), 1.0f);
}
