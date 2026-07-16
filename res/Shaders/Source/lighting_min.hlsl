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

[[vk::combinedImageSampler]] Texture2D sceneDepth : register(t5, space0);
[[vk::combinedImageSampler]] SamplerState sceneDepthSampler : register(s5, space0);

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

float3 decodeNormal(float3 encodedNormal)
{
    return normalize(encodedNormal * 2.0f - 1.0f);
}

float4 PSMain(VSOutput input) : SV_Target
{
    float3 albedo = gBufferAlbedo.Sample(gBufferAlbedoSampler, input.uv).rgb;
    float3 normal = decodeNormal(gBufferNormal.Sample(gBufferNormalSampler, input.uv).rgb);
    float4 material = gBufferMaterial.Sample(gBufferMaterialSampler, input.uv);

    float ao = saturate(material.b);

    float3 L = normalize(-lightDir.xyz);
    float NdotL = saturate(dot(normal, L));

    float3 diffuse = albedo * NdotL * lightColor.rgb * lightColor.a;
    float3 ambient = albedo * 0.04f * ao;

    return float4(ambient + diffuse, 1.0f);
}
