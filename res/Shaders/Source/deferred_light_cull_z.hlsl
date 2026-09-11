#include "light_culling.hlsli"

RWStructuredBuffer<uint> lightCullZMasks : register(u1, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint slice = dispatchThreadId.x;
    if (slice >= TASROVY_LIGHT_DEPTH_SLICES)
        return;

    float nearPlane = gpuCameraPositionAndNear.w;
    float farPlane = gpuRenderSizeAndFar.w;
    float sliceNear = lerp(
        nearPlane, farPlane,
        (float)slice / (float)TASROVY_LIGHT_DEPTH_SLICES);
    float sliceFar = lerp(
        nearPlane, farPlane,
        (float)(slice + 1u) / (float)TASROVY_LIGHT_DEPTH_SLICES);

    uint masks[TASROVY_LIGHT_MASK_WORDS];
    [unroll]
    for (uint word = 0u; word < TASROVY_LIGHT_MASK_WORDS; ++word)
        masks[word] = 0u;

    GpuSceneLightData sceneLighting = gpuSceneLights[0];
    uint lightCount = min((uint)round(sceneLighting.meta.x), TASROVY_MAX_LIGHTS);
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        GpuSceneLight light = sceneLighting.lights[lightIndex];
        uint lightType = (uint)round(light.positionAndType.w);
        bool intersects = lightType == 0u;
        if (!intersects)
        {
            float viewDepth = -mul(
                float4(light.positionAndType.xyz, 1.0f), gpuView).z;
            float radius = LightInfluenceRadius(light);
            intersects = viewDepth + radius >= sliceNear &&
                         viewDepth - radius <= sliceFar;
        }
        if (intersects)
            masks[lightIndex >> 5u] |= 1u << (lightIndex & 31u);
    }

    [unroll]
    for (uint word = 0u; word < TASROVY_LIGHT_MASK_WORDS; ++word)
        lightCullZMasks[slice * TASROVY_LIGHT_MASK_WORDS + word] = masks[word];
}
