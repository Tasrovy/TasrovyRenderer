#include "light_culling.hlsli"

RWStructuredBuffer<uint> lightCullXYMasks : register(u1, space0);

bool LightIntersectsTile(
    GpuSceneLight light,
    uint2 tile,
    uint2 renderExtent)
{
    uint lightType = (uint)round(light.positionAndType.w);
    if (lightType == 0u)
        return true;

    float radius = LightInfluenceRadius(light);
    if (radius <= 0.0f)
        return false;

    float3 centerVS = mul(float4(light.positionAndType.xyz, 1.0f), gpuView).xyz;
    float viewDepth = -centerVS.z;
    float nearPlane = gpuCameraPositionAndNear.w;
    float farPlane = gpuRenderSizeAndFar.w;
    if (viewDepth + radius < nearPlane || viewDepth - radius > farPlane)
        return false;
    // A sphere crossing the camera plane can cover an arbitrarily large
    // screen region. Keep it conservatively visible in every tile.
    if (viewDepth <= radius + nearPlane)
        return true;

    float4 clip = mul(float4(centerVS, 1.0f), gpuProjection);
    if (abs(clip.w) < 0.0001f)
        return true;

    float2 centerUv = clip.xy / clip.w * 0.5f + 0.5f;
    float safeDepth = max(viewDepth - radius, nearPlane);
    float2 radiusUv = 0.5f * abs(float2(
        gpuProjection[0][0], gpuProjection[1][1])) * radius / safeDepth;

    float2 tileMin = float2(tile * TASROVY_LIGHT_TILE_SIZE) /
        float2(renderExtent);
    float2 tileMax = float2(min(
        (tile + 1u) * TASROVY_LIGHT_TILE_SIZE, renderExtent)) /
        float2(renderExtent);
    return all(centerUv + radiusUv >= tileMin) &&
           all(centerUv - radiusUv <= tileMax);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 renderExtent = max((uint2)gpuRenderSizeAndFar.xy, uint2(1u, 1u));
    uint2 tileCount = (renderExtent + TASROVY_LIGHT_TILE_SIZE - 1u) /
        TASROVY_LIGHT_TILE_SIZE;
    uint2 tile = dispatchThreadId.xy;
    if (any(tile >= tileCount))
        return;

    uint tileIndex = tile.y * tileCount.x + tile.x;
    uint masks[TASROVY_LIGHT_MASK_WORDS];
    [unroll]
    for (uint word = 0u; word < TASROVY_LIGHT_MASK_WORDS; ++word)
        masks[word] = 0u;

    GpuSceneLightData sceneLighting = gpuSceneLights[0];
    uint lightCount = min((uint)round(sceneLighting.meta.x), TASROVY_MAX_LIGHTS);
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        if (LightIntersectsTile(
                sceneLighting.lights[lightIndex], tile, renderExtent))
        {
            masks[lightIndex >> 5u] |= 1u << (lightIndex & 31u);
        }
    }

    [unroll]
    for (uint word = 0u; word < TASROVY_LIGHT_MASK_WORDS; ++word)
        lightCullXYMasks[tileIndex * TASROVY_LIGHT_MASK_WORDS + word] =
            masks[word];
}
