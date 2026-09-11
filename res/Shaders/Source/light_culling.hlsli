#ifndef TASROVY_LIGHT_CULLING_HLSLI
#define TASROVY_LIGHT_CULLING_HLSLI

#include "gpu_scene.hlsli"

static const uint TASROVY_MAX_LIGHTS = 256u;
static const uint TASROVY_LIGHT_MASK_WORDS = 8u;
static const uint TASROVY_LIGHT_TILE_SIZE = 32u;
static const uint TASROVY_LIGHT_DEPTH_SLICES = 1024u;

float LightInfluenceRadius(GpuSceneLight light)
{
    uint lightType = (uint)round(light.positionAndType.w);
    if (lightType == 0u)
        return gpuRenderSizeAndFar.w;

    float peak = max(max(light.colorAndIntensity.r, light.colorAndIntensity.g),
                     light.colorAndIntensity.b) * max(light.colorAndIntensity.w, 0.0f);
    // A contribution below roughly one display-encoded 10-bit step is not
    // useful for tile membership. The result is clamped to the camera range.
    float minimumContribution = 1.0f / 1024.0f;

    if (lightType == 2u)
    {
        float area = max(light.parameters.x * light.parameters.y, 0.0001f);
        float halfDiagonal = 0.5f * length(light.parameters.xy);
        return min(sqrt(max(peak * area / minimumContribution, 0.0f)) +
                   halfDiagonal, gpuRenderSizeAndFar.w);
    }

    float targetDenominator = peak / minimumContribution;
    float constantTerm = max(light.parameters.x, 0.0001f);
    float linearTerm = max(light.parameters.y, 0.0f);
    float quadraticTerm = max(light.parameters.z, 0.0f);
    float radius = 0.0f;
    if (quadraticTerm > 0.000001f)
    {
        float discriminant = linearTerm * linearTerm -
            4.0f * quadraticTerm * (constantTerm - targetDenominator);
        radius = (-linearTerm + sqrt(max(discriminant, 0.0f))) /
            (2.0f * quadraticTerm);
    }
    else if (linearTerm > 0.000001f)
    {
        radius = (targetDenominator - constantTerm) / linearTerm;
    }
    return clamp(radius, 0.0f, gpuRenderSizeAndFar.w);
}

uint LightDepthSlice(float viewDepth)
{
    float nearPlane = gpuCameraPositionAndNear.w;
    float farPlane = gpuRenderSizeAndFar.w;
    float normalizedDepth = saturate(
        (viewDepth - nearPlane) / max(farPlane - nearPlane, 0.0001f));
    return min((uint)(normalizedDepth * TASROVY_LIGHT_DEPTH_SLICES),
               TASROVY_LIGHT_DEPTH_SLICES - 1u);
}

#endif
