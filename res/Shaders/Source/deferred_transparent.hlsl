#include "gpu_scene.hlsli"

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 tangent : TANGENT;
    [[vk::location(3)]] float3 bitangent : BITANGENT;
    [[vk::location(4)]] float2 uv0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 uv0 : TEXCOORD0;
    nointerpolation uint objectIndex : TEXCOORD1;
};

[[vk::combinedImageSampler]] Texture2D baseColorTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s1, space0);

[[vk::combinedImageSampler]] Texture2D normalTexture : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState normalSampler : register(s2, space0);

[[vk::combinedImageSampler]] Texture2D emissiveTexture : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState emissiveSampler : register(s3, space0);

[[vk::combinedImageSampler]] Texture2D metallicRoughnessAOTexture : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState metallicRoughnessAOSampler : register(s4, space0);

VSOutput VSMain(VSInput input, uint objectIndex : SV_InstanceID)
{
    VSOutput output;
    GpuObjectData object = gpuObjects[objectIndex];
    matrix projection = (object.flags & 1u) != 0u
        ? gpuProjection : gpuUnflippedProjection;
    output.position = mul(mul(mul(
        float4(input.position, 1.0f), object.model), gpuView), projection);
    output.normal = normalize(mul(float4(input.normal, 0.0f), object.model).xyz);
    output.color = float3(1.0f, 1.0f, 1.0f);
    output.uv0 = input.uv0;
    output.objectIndex = objectIndex;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    GpuObjectData object = gpuObjects[input.objectIndex];
    GpuMaterialData material = gpuMaterials[object.materialIndex];
    GpuSceneLightData sceneLighting = gpuSceneLights[0];
    float2 baseColorUv = ApplyTextureUV(
        input.uv0,
        material.baseColorUvTransform,
        material.textureUvModes.x);
    float2 emissiveUv = ApplyTextureUV(
        input.uv0,
        material.emissiveUvTransform,
        material.textureUvModes.z);
    float4 sampledBaseColor = baseColorTexture.Sample(baseColorSampler, baseColorUv);
    float4 baseColor = material.baseColorFactorAndTexture.w > 0.5f
        ? sampledBaseColor * float4(material.baseColorFactorAndTexture.rgb, 1.0f)
        : float4(material.baseColorFactorAndTexture.rgb, 1.0f);
    float3 emissive = emissiveTexture.Sample(emissiveSampler, emissiveUv).rgb;
    float3 normal = normalize(input.normal);

    float3 L = normalize(-sceneLighting.primaryDirection.xyz);
    float NdotL = saturate(dot(normal, L));
    float3 lit = baseColor.rgb * (0.08f + NdotL *
        sceneLighting.primaryColor.rgb * sceneLighting.primaryColor.a) + emissive;

    return float4(saturate(lit), baseColor.a);
}
