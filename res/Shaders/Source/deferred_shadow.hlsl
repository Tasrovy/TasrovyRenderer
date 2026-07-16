cbuffer UBO : register(b0, space0)
{
    matrix model;
    matrix view;
    matrix proj;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(mul(mul(float4(input.position, 1.0f), model), view), proj);
    return output;
}

void PSMain(VSOutput input)
{
}
