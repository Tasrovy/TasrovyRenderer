cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
    float4 lightColor;
    float4 camPosAndMetallic;
    float4 roughnessAo;
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

float4 PSMain(VSOutput input) : SV_Target
{
    float3 albedo = gBufferAlbedo.Sample(gBufferAlbedoSampler, input.uv).rgb;
    float3 normal = DecodeNormal(gBufferNormal.Sample(gBufferNormalSampler, input.uv).rgb);
    float4 material = gBufferMaterial.Sample(gBufferMaterialSampler, input.uv);
    float3 worldPos = gBufferWorldPos.Sample(gBufferWorldPosSampler, input.uv).xyz;

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

    float3 L = normalize(-lightDir.xyz);
    float3 V = normalize(camPosAndMetallic.xyz - worldPos);
    float3 H = normalize(L + V);
    float3 R = reflect(-V, normal);

    float NdotL = saturate(dot(normal, L));
    float NdotV = max(dot(normal, V), 0.0f);

    float3 F0 = lerp(0.04f.xxx, albedo, metallic);
    float D = DistributionGGX(normal, H, roughness);
    float G = GeometrySmith(normal, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = D * G * F;
    float denominator = 4.0f * max(NdotV, 0.001f) * max(NdotL, 0.001f);
    float3 specularDirect = numerator / max(denominator, 0.001f);

    float3 kS = F;
    float3 kD = (1.0f.xxx - kS) * (1.0f - metallic);
    float3 diffuseDirect = kD * albedo / PI;
    float3 radiance = lightColor.rgb * lightColor.a * NdotL;
    float3 direct = (diffuseDirect + specularDirect) * radiance;

    const float MAX_REFLECTION_LOD = 7.0f;
    float3 irradiance = irradianceMap.Sample(irradianceSampler, normal).rgb;
    float3 diffuseIBL = kD * irradiance * albedo;

    float3 prefilteredColor =
        prefilteredMap.SampleLevel(prefilteredSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = brdfLUT.Sample(brdfSampler, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    float3 color = (diffuseIBL + specularIBL) * ao + direct;
    return float4(saturate(color), 1.0f);
}
