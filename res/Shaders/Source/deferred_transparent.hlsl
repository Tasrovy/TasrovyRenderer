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
};

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
};

[[vk::combinedImageSampler]] Texture2D baseColorTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s1, space0);

[[vk::combinedImageSampler]] Texture2D normalTexture : register(t2, space0);
[[vk::combinedImageSampler]] SamplerState normalSampler : register(s2, space0);

[[vk::combinedImageSampler]] Texture2D emissiveTexture : register(t3, space0);
[[vk::combinedImageSampler]] SamplerState emissiveSampler : register(s3, space0);

[[vk::combinedImageSampler]] Texture2D metallicRoughnessAOTexture : register(t4, space0);
[[vk::combinedImageSampler]] SamplerState metallicRoughnessAOSampler : register(s4, space0);

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
    output.position = mul(mul(mul(float4(input.position, 1.0f), model), view), proj);
    output.normal = normalize(mul(float4(input.normal, 0.0f), model).xyz);
    output.color = float3(1.0f, 1.0f, 1.0f);
    output.uv0 = input.uv0;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 materialUv = ResolveMaterialUV(input.uv0);
    float4 sampledBaseColor = baseColorTexture.Sample(baseColorSampler, materialUv);
    float4 baseColor = baseColorFactorAndTexture.w > 0.5f
        ? sampledBaseColor * float4(baseColorFactorAndTexture.rgb, 1.0f)
        : float4(baseColorFactorAndTexture.rgb, 1.0f);
    float3 emissive = emissiveTexture.Sample(emissiveSampler, materialUv).rgb;
    float3 normal = normalize(input.normal);

    float3 L = normalize(-lightDir.xyz);
    float NdotL = saturate(dot(normal, L));
    float3 lit = baseColor.rgb * (0.08f + NdotL * lightColor.rgb * lightColor.a) + emissive;

    return float4(saturate(lit), baseColor.a);
}
