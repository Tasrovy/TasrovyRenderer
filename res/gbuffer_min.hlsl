cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 color : COLOR;
    float2 uv0 : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 uv0 : TEXCOORD0;
};

struct PSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(mul(float4(input.position, 1.0f), model), view), proj);
    output.normal = normalize(input.normal);
    output.color = input.color;
    output.uv0 = input.uv0;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    output.albedo = float4(input.color, 1.0f);
    output.normal = float4(normalize(input.normal) * 0.5f + 0.5f, 1.0f);
    output.material = float4(0.5f, 0.5f, 1.0f, 1.0f);
    return output;
}
