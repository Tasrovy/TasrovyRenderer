#ifndef TASROVY_POSTPROCESS_SSR
#define TASROVY_POSTPROCESS_SSR

float2 ProjectWorldToUv(float3 worldPosition, out bool valid)
{
    float4 clip = mul(mul(float4(worldPosition, 1.0f), view), proj);
    valid = clip.w > 0.0f;
    float2 uv = clip.xy / max(clip.w, 0.0001f) * 0.5f + 0.5f;
    valid = valid && all(uv > 0.0f.xx) && all(uv < 1.0f.xx);
    return uv;
}

float SampleHiZMinimum(uint level, float2 uv)
{
    uv = saturate(uv);
    uint width;
    uint height;
    if (level == 3u) {
        hiZSixteenth.GetDimensions(width, height);
        uint2 coord = min(uint2(uv * float2(width, height)), uint2(width - 1u, height - 1u));
        return hiZSixteenth.Load(int3(coord, 0)).r;
    }
    if (level == 2u) {
        hiZEighth.GetDimensions(width, height);
        uint2 coord = min(uint2(uv * float2(width, height)), uint2(width - 1u, height - 1u));
        return hiZEighth.Load(int3(coord, 0)).r;
    }
    if (level == 1u) {
        hiZQuarter.GetDimensions(width, height);
        uint2 coord = min(uint2(uv * float2(width, height)), uint2(width - 1u, height - 1u));
        return hiZQuarter.Load(int3(coord, 0)).r;
    }
    hiZHalf.GetDimensions(width, height);
    uint2 coord = min(uint2(uv * float2(width, height)), uint2(width - 1u, height - 1u));
    return hiZHalf.Load(int3(coord, 0)).r;
}

float3 ApplyScreenSpaceReflection(float2 uv, float3 baseColor)
{
    if (advancedLightingParams.w < 0.5f) {
        return baseColor;
    }

    float depth = sceneDepth.SampleLevel(sceneDepthSampler, uv, 0.0f).r;
    if (depth >= 0.99999f) {
        return baseColor;
    }

    float3 worldPosition =
        gBufferWorldPos.SampleLevel(gBufferWorldPosSampler, uv, 0.0f).xyz;
    float3 normal = normalize(
        gBufferNormal.SampleLevel(gBufferNormalSampler, uv, 0.0f).xyz * 2.0f - 1.0f);
    float4 material = gBufferMaterial.SampleLevel(gBufferMaterialSampler, uv, 0.0f);
    float roughness = saturate(material.g);
    float metallic = saturate(material.r);
    if (roughness > 0.98f) {
        return baseColor;
    }

    float3 viewDirection = normalize(camPosAndMetallic.xyz - worldPosition);
    float3 rayDirection = normalize(reflect(-viewDirection, normal));
    float3 rayPosition = worldPosition + normal * max(ssrParams.z * 0.25f, 0.01f);
    float traveled = 0.0f;
    float2 hitUv = uv;
    bool hit = false;
    float3 previousRayPosition = rayPosition;

    [loop]
    for (int stepIndex = 0; stepIndex < 128; ++stepIndex) {
        traveled += ssrParams.y;
        if (traveled > ssrParams.x) {
            break;
        }
        previousRayPosition = rayPosition;
        rayPosition += rayDirection * ssrParams.y;
        bool valid;
        float2 rayUv = ProjectWorldToUv(rayPosition, valid);
        if (!valid) {
            break;
        }

        float rayViewZ = mul(float4(rayPosition, 1.0f), view).z;
        float rayLinearDepth = max(-rayViewZ, 0.0f);
        float footprint = max(traveled / max(ssrParams.y * 16.0f, 0.0001f), 1.0f);
        uint hiZLevel = min((uint)floor(log2(footprint)), 3u);
        float nearestCellDepth = SampleHiZMinimum(hiZLevel, rayUv);
        if (nearestCellDepth >= 65500.0f ||
            rayLinearDepth + ssrParams.z < nearestCellDepth) {
            continue;
        }

        float sampleDepth = sceneDepth.SampleLevel(sceneDepthSampler, rayUv, 0.0f).r;
        if (sampleDepth >= 0.99999f) {
            continue;
        }
        float3 scenePosition =
            gBufferWorldPos.SampleLevel(gBufferWorldPosSampler, rayUv, 0.0f).xyz;
        float sceneViewZ = mul(float4(scenePosition, 1.0f), view).z;
        float depthDelta = sceneViewZ - rayViewZ;
        if (depthDelta >= 0.0f && traveled > ssrParams.y * 2.0f) {
            float3 frontPosition = previousRayPosition;
            float3 backPosition = rayPosition;
            float2 refinedUv = rayUv;
            [unroll]
            for (int refineIndex = 0; refineIndex < 6; ++refineIndex) {
                float3 midpoint = (frontPosition + backPosition) * 0.5f;
                bool midpointValid;
                float2 midpointUv = ProjectWorldToUv(midpoint, midpointValid);
                if (!midpointValid) {
                    break;
                }
                float midpointDepth =
                    sceneDepth.SampleLevel(sceneDepthSampler, midpointUv, 0.0f).r;
                if (midpointDepth >= 0.99999f) {
                    frontPosition = midpoint;
                    continue;
                }
                float3 midpointScenePosition =
                    gBufferWorldPos.SampleLevel(gBufferWorldPosSampler, midpointUv, 0.0f).xyz;
                float midpointRayZ = mul(float4(midpoint, 1.0f), view).z;
                float midpointSceneZ = mul(float4(midpointScenePosition, 1.0f), view).z;
                if (midpointSceneZ - midpointRayZ >= 0.0f) {
                    backPosition = midpoint;
                    refinedUv = midpointUv;
                } else {
                    frontPosition = midpoint;
                }
            }

            float3 hitPosition =
                gBufferWorldPos.SampleLevel(gBufferWorldPosSampler, refinedUv, 0.0f).xyz;
            float refinedRayZ = mul(float4(backPosition, 1.0f), view).z;
            float refinedSceneZ = mul(float4(hitPosition, 1.0f), view).z;
            float3 hitNormal = normalize(
                gBufferNormal.SampleLevel(gBufferNormalSampler, refinedUv, 0.0f).xyz * 2.0f - 1.0f);
            const bool frontFacingHit = dot(hitNormal, -rayDirection) > 0.03f;
            if (frontFacingHit && abs(refinedSceneZ - refinedRayZ) < ssrParams.z) {
                hitUv = refinedUv;
                hit = true;
                break;
            }
        }
    }

    if (!hit) {
        return baseColor;
    }

    float3 reflectedColor =
        sceneColor.SampleLevel(sceneColorSampler, hitUv, 0.0f).rgb;
    float2 edgeDistance = min(hitUv, 1.0f.xx - hitUv);
    float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) * 12.0f);
    float fresnel = pow(1.0f - saturate(dot(normal, viewDirection)), 5.0f);
    float reflectivity = (1.0f - roughness) * saturate(metallic + 0.15f + fresnel);
    float blend = saturate(ssrParams.w * reflectivity * edgeFade);
    return lerp(baseColor, reflectedColor, blend);
}

#endif
