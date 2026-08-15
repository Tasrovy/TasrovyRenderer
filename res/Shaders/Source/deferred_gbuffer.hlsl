#include "gpu_scene.hlsli"

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(4)]] float2 uv0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : NORMAL;
    float2 uv0 : TEXCOORD1;
    float4 currentClip : TEXCOORD2;
    float4 previousClip : TEXCOORD3;
    nointerpolation uint objectIndex : TEXCOORD4;
};

struct PSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 worldPos : SV_Target3;
    float4 effects : SV_Target4;
    float2 velocity : SV_Target5;
};

[[vk::combinedImageSampler]] Texture2D baseColorTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s1, space0);

VSOutput VSMain(VSInput input, uint objectIndex : SV_InstanceID)
{
    VSOutput output;
    GpuObjectData object = gpuObjects[objectIndex];
    matrix projection = (object.flags & 1u) != 0u
        ? gpuProjection : gpuUnflippedProjection;
    matrix previousProjection = (object.flags & 1u) != 0u
        ? gpuPreviousProjection : gpuPreviousUnflippedProjection;
    output.worldPos = mul(float4(input.position, 1.0f), object.model).xyz;
    output.position = mul(mul(float4(output.worldPos, 1.0f), gpuView), projection);
    float3 previousWorldPos =
        mul(float4(input.position, 1.0f), object.previousModel).xyz;
    output.currentClip = output.position;
    output.previousClip =
        mul(mul(float4(previousWorldPos, 1.0f), gpuPreviousView), previousProjection);
    output.normal = normalize(mul(float4(input.normal, 0.0f), object.model).xyz);
    output.uv0 = input.uv0;
    output.objectIndex = objectIndex;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    GpuObjectData object = gpuObjects[input.objectIndex];
    GpuMaterialData material = gpuMaterials[object.materialIndex];
    float2 materialUv = ApplyTextureUV(
        input.uv0,
        material.baseColorUvTransform,
        material.textureUvModes.x);
    // taaParams.z carries a negative mip bias only for TAAU. At native
    // resolution it is zero, preserving the regular hardware LOD choice.
    float4 sampledBaseColor = baseColorTexture.SampleBias(
        baseColorSampler, materialUv, gpuJitterAndMipBias.z);
    float4 baseColor = material.baseColorFactorAndTexture.w > 0.5f
        ? sampledBaseColor * float4(material.baseColorFactorAndTexture.rgb, 1.0f)
        : float4(material.baseColorFactorAndTexture.rgb, 1.0f);

    float metallic = saturate(material.surface.x);
    float roughness = saturate(material.surface.y);
    float ao = saturate(material.surface.z);
    float emissiveIntensity = max(material.emission.x, 0.0f);
    // GBuffer material alpha stores the 8-bit shading-model ID normalized.
    float shadingModelId = emissiveIntensity > 0.0f ? (1.0f / 255.0f) : 0.0f;
    if (emissiveIntensity > 0.0f) {
        baseColor.rgb *= emissiveIntensity;
    }

    float3 N = normalize(input.normal);
    float2 velocity = float2(0.0f, 0.0f);
    if (input.currentClip.w > 0.0001f && input.previousClip.w > 0.0001f) {
        float2 currentUv =
            input.currentClip.xy / input.currentClip.w * 0.5f + 0.5f;
        float2 previousUv =
            input.previousClip.xy / input.previousClip.w * 0.5f + 0.5f;
        velocity = currentUv - previousUv;
    }

    PSOutput output;
    output.albedo = float4(baseColor.rgb, 1.0f);
    output.normal = float4(N * 0.5f + 0.5f, 1.0f);
    output.material = float4(metallic, roughness, ao, shadingModelId);
    output.worldPos = float4(input.worldPos, max(material.rimParams.x, 0.25f));
    output.effects = material.rimColorAndStrength;
    output.velocity = velocity;
    return output;
}
