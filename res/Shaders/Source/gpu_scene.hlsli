#ifndef TASROVY_GPU_SCENE_HLSLI
#define TASROVY_GPU_SCENE_HLSLI

struct GpuSceneLight
{
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndIntensity;
    float4 parameters;
};

struct GpuObjectData
{
    matrix model;
    matrix previousModel;
    uint materialIndex;
    uint flags;
    uint2 padding;
};

struct GpuMaterialData
{
    float4 baseColorFactorAndTexture;
    float4 surface;
    float4 emission;
    float4 rimColorAndStrength;
    float4 rimParams;
    float4 baseColorUvTransform;
    float4 normalUvTransform;
    float4 emissiveUvTransform;
    float4 mraUvTransform;
    float4 textureUvModes;
};

float2 ApplyTextureUV(float2 uv, float4 transform, float modeValue)
{
    uint mode = (uint)round(modeValue);
    float2 orientedUv = uv;
    if (mode == 1) orientedUv = float2(uv.x, 1.0f - uv.y);
    else if (mode == 2) orientedUv = float2(1.0f - uv.x, uv.y);
    else if (mode == 3) orientedUv = float2(1.0f - uv.x, 1.0f - uv.y);
    else if (mode == 4) orientedUv = float2(uv.y, uv.x);
    else if (mode == 5) orientedUv = float2(uv.y, 1.0f - uv.x);
    else if (mode == 6) orientedUv = float2(1.0f - uv.y, uv.x);
    return orientedUv * transform.xy + transform.zw;
}

struct GpuSceneLightData
{
    float4 meta;
    float4 primaryDirection;
    float4 primaryColor;
    GpuSceneLight lights[8];
};

cbuffer ViewUniform : register(b20, space0)
{
    matrix gpuView;
    matrix gpuProjection;
    matrix gpuUnflippedProjection;
    matrix gpuPreviousView;
    matrix gpuPreviousProjection;
    matrix gpuPreviousUnflippedProjection;
    float4 gpuCameraPositionAndNear;
    float4 gpuRenderSizeAndFar;
    float4 gpuJitterAndMipBias;
};

StructuredBuffer<GpuObjectData> gpuObjects : register(t21, space0);
StructuredBuffer<GpuMaterialData> gpuMaterials : register(t22, space0);
StructuredBuffer<GpuSceneLightData> gpuSceneLights : register(t23, space0);

#endif
