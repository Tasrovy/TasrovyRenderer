cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
    float4 lightDir;
    float4 lightColor;
    float4 camPos;
    float metallic;
    float roughness;
    float ao;
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
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(mul(float4(input.position, 1.0f), model), view), proj);
    output.normal = normalize(input.normal);
    output.color = float3(1.0f, 1.0f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float ndotl = saturate(dot(normalize(input.normal), normalize(-lightDir.xyz)));
    float3 lit = input.color * (0.15f + ndotl * lightColor.rgb * lightColor.a * 0.1f);
    return float4(lit, 1.0f);
}
