struct FrameData
{
    matrix view;
    matrix proj;
    matrix previousView;
    matrix previousProj;
    float4 uvTransform;
    float4 taaParams;
    uint drawCount;
    uint3 padding;
};

struct DrawData
{
    matrix model;
    matrix previousModel;
    float4 baseColorFactorAndTexture;
    float4 materialParams;
    float4 materialEmission;
    float4 rimColorAndStrength;
    float4 rimParams;
    float4 worldBounds;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

cbuffer FrameUBO : register(b0, space0) { FrameData frameData; }
[[vk::combinedImageSampler]] Texture2D baseColorTexture : register(t1, space0);
[[vk::combinedImageSampler]] SamplerState baseColorSampler : register(s1, space0);
StructuredBuffer<DrawData> draws : register(t2, space0);

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
    nointerpolation uint drawIndex : TEXCOORD4;
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

VSOutput VSMain(VSInput input, uint instanceId : SV_InstanceID)
{
    DrawData draw = draws[instanceId];
    VSOutput output;
    output.drawIndex = instanceId;
    output.worldPos = mul(float4(input.position, 1.0f), draw.model).xyz;
    output.position = mul(mul(float4(output.worldPos, 1.0f), frameData.view), frameData.proj);
    float3 previousWorldPos =
        mul(float4(input.position, 1.0f), draw.previousModel).xyz;
    output.currentClip = output.position;
    output.previousClip = mul(
        mul(float4(previousWorldPos, 1.0f), frameData.previousView),
        frameData.previousProj);
    output.normal = normalize(mul(float4(input.normal, 0.0f), draw.model).xyz);
    output.uv0 = input.uv0;
    return output;
}

PSOutput PSMain(VSOutput input)
{
    DrawData draw = draws[input.drawIndex];
    float2 materialUv = input.uv0;
    float4 sampled = baseColorTexture.SampleBias(
        baseColorSampler, materialUv, frameData.taaParams.z);
    float4 baseColor = draw.baseColorFactorAndTexture.w > 0.5f
        ? sampled * float4(draw.baseColorFactorAndTexture.rgb, 1.0f)
        : float4(draw.baseColorFactorAndTexture.rgb, 1.0f);
    float emissive = max(draw.materialEmission.x, 0.0f);
    if (emissive > 0.0f) baseColor.rgb *= emissive;

    float2 velocity = 0.0f;
    if (input.currentClip.w > 0.0001f && input.previousClip.w > 0.0001f) {
        float2 currentUv = input.currentClip.xy / input.currentClip.w * 0.5f + 0.5f;
        float2 previousUv = input.previousClip.xy / input.previousClip.w * 0.5f + 0.5f;
        velocity = currentUv - previousUv;
    }

    PSOutput output;
    output.albedo = float4(baseColor.rgb, 1.0f);
    output.normal = float4(normalize(input.normal) * 0.5f + 0.5f, 1.0f);
    output.material = float4(
        saturate(draw.materialParams.x),
        saturate(draw.materialParams.y),
        saturate(draw.materialParams.z),
        emissive > 0.0f ? 1.0f / 255.0f : 0.0f);
    output.worldPos = float4(input.worldPos, max(draw.rimParams.x, 0.25f));
    output.effects = draw.rimColorAndStrength;
    output.velocity = velocity;
    return output;
}
