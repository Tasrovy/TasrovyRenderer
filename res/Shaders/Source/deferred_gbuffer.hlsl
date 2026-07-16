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
};

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
};

struct PSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 worldPos : SV_Target3;
};

[[vk::combinedImageSampler]] Texture2D baseColorTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s1, space0);

float2 ResolveMaterialUV(float2 uv)
{
    uint mode = (uint)round(roughnessAo.w);
    float2 orientedUv = uv;
    if (mode == 1) {
        orientedUv = float2(uv.x, 1.0f - uv.y);
    }
    else if (mode == 2) {
        orientedUv = float2(1.0f - uv.x, uv.y);
    }
    else if (mode == 3) {
        orientedUv = float2(1.0f - uv.x, 1.0f - uv.y);
    }
    else if (mode == 4) {
        orientedUv = float2(uv.y, uv.x);
    }
    else if (mode == 5) {
        orientedUv = float2(uv.y, 1.0f - uv.x);
    }
    else if (mode == 6) {
        orientedUv = float2(1.0f - uv.y, uv.x);
    }
    return frac(orientedUv * uvTransform.xy + uvTransform.zw);
}
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.worldPos = mul(float4(input.position, 1.0f), model).xyz;
    output.position = mul(mul(float4(output.worldPos, 1.0f), view), proj);
    output.normal = normalize(mul(float4(input.normal, 0.0f), model).xyz);
    output.uv0 = input.uv0;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    float2 materialUv = ResolveMaterialUV(input.uv0);
    float4 baseColor = baseColorTexture.Sample(baseColorSampler, materialUv);
    uint debugMode = (uint)round(roughnessAo.z);

    float metallic = saturate(camPosAndMetallic.w);
    float roughness = saturate(roughnessAo.x);
    float ao = saturate(roughnessAo.y);
    float shadingModelId = 0.0f;

    float3 N = normalize(input.normal);

    PSOutput output;
    if (debugMode == 2) {
        output.albedo = float4(frac(input.uv0), 0.0f, 1.0f);
    } else if (debugMode == 3) {
        output.albedo = float4(frac(materialUv), 0.0f, 1.0f);
    } else {
        output.albedo = float4(baseColor.rgb, baseColor.a);
    }
    output.normal = float4(N * 0.5f + 0.5f, 1.0f);
    output.material = float4(metallic, roughness, ao, shadingModelId);
    output.worldPos = float4(input.worldPos, 1.0f);
    return output;
}
