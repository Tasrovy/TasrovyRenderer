#include "gpu_scene.hlsli"

cbuffer ShadowPassConstants : register(b0, space0)
{
    matrix unusedModel;
    matrix shadowView;
    matrix shadowProjection;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input, uint objectIndex : SV_InstanceID)
{
    VSOutput output;
    output.position = mul(mul(mul(
        float4(input.position, 1.0f), gpuObjects[objectIndex].model),
        shadowView), shadowProjection);
    return output;
}

void PSMain(VSOutput input)
{
}
