#include "ShadowViewSystem.h"

#include "../render/Camera.h"

#include <algorithm>
#include <cmath>

namespace Tasrovy::Renderer {

using namespace Tasrovy::Base;

namespace {

TSMat4f orthographicProjection(
    float left,
    float right,
    float bottom,
    float top,
    float nearPlane,
    float farPlane) {
    TSMat4f result(1.0f);
    result[0][0] = 2.0f / (right - left);
    result[1][1] = 2.0f / (top - bottom);
    result[2][2] = -1.0f / (farPlane - nearPlane);
    result[3][0] = -(right + left) / (right - left);
    result[3][1] = -(top + bottom) / (top - bottom);
    result[3][2] = -nearPlane / (farPlane - nearPlane);
    return result;
}

} // namespace

ShadowViewData ShadowViewSystem::build(
    const Tasrovy::Render::Camera& camera,
    const TSVec3f& lightDirection,
    float maximumDistance,
    float splitLambda,
    bool cascadesEnabled) const {
    ShadowViewData result;
    const float nearPlane = std::max(camera.getNearPlane(), 0.01f);
    const float farPlane = std::max(
        nearPlane + 0.01f,
        std::min(camera.getFarPlane(), maximumDistance));
    const float range = farPlane - nearPlane;
    const float ratio = farPlane / nearPlane;
    if (cascadesEnabled) {
        for (size_t cascade = 0; cascade < ShadowCascadeCount; ++cascade) {
            const float fraction =
                static_cast<float>(cascade + 1) /
                static_cast<float>(ShadowCascadeCount);
            const float logarithmic = nearPlane * std::pow(ratio, fraction);
            const float uniform = nearPlane + range * fraction;
            result.splits[cascade] =
                splitLambda * logarithmic + (1.0f - splitLambda) * uniform;
        }
    } else {
        result.splits.fill(farPlane);
    }

    const TSMat4f inverseView = inverse(camera.getViewMatrix());
    const float tanHalfFov = std::tan(radians(camera.getFOV()) * 0.5f);
    const TSVec3f direction = normalize(lightDirection);
    const TSVec3f up = std::abs(direction.y) > 0.95f
        ? TSVec3f(0.0f, 0.0f, 1.0f)
        : TSVec3f(0.0f, 1.0f, 0.0f);
    const TSVec3f lightRight = normalize(cross(direction, up));
    const TSVec3f lightUp = normalize(cross(lightRight, direction));

    float cascadeNear = nearPlane;
    const size_t cascadeBuildCount =
        cascadesEnabled ? ShadowCascadeCount : 1;
    for (size_t cascade = 0; cascade < cascadeBuildCount; ++cascade) {
        const float cascadeFar = result.splits[cascade];
        const float nearHalfHeight = tanHalfFov * cascadeNear;
        const float nearHalfWidth = nearHalfHeight * camera.getAspect();
        const float farHalfHeight = tanHalfFov * cascadeFar;
        const float farHalfWidth = farHalfHeight * camera.getAspect();

        std::array<TSVec3f, 8> corners{};
        size_t cornerIndex = 0;
        for (const float depth : {cascadeNear, cascadeFar}) {
            const float halfWidth =
                depth == cascadeNear ? nearHalfWidth : farHalfWidth;
            const float halfHeight =
                depth == cascadeNear ? nearHalfHeight : farHalfHeight;
            for (const float y : {-halfHeight, halfHeight}) {
                for (const float x : {-halfWidth, halfWidth}) {
                    const auto world =
                        inverseView * TSVec4f(x, y, -depth, 1.0f);
                    corners[cornerIndex++] =
                        TSVec3f(world.x, world.y, world.z) / world.w;
                }
            }
        }

        TSVec3f center(0.0f);
        for (const auto& corner : corners) {
            center += corner;
        }
        center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const auto& corner : corners) {
            radius = std::max(radius, length(corner - center));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;
        radius = std::max(radius, 0.1f);

        const float worldUnitsPerTexel =
            (radius * 2.0f) / static_cast<float>(ShadowMapResolution);
        const float centerRight = dot(center, lightRight);
        const float centerUp = dot(center, lightUp);
        const float snappedRight =
            std::round(centerRight / worldUnitsPerTexel) * worldUnitsPerTexel;
        const float snappedUp =
            std::round(centerUp / worldUnitsPerTexel) * worldUnitsPerTexel;
        center += lightRight * (snappedRight - centerRight);
        center += lightUp * (snappedUp - centerUp);

        const float depthPadding = std::max(10.0f, radius * 0.5f);
        const float lightDistance = radius + depthPadding;
        result.views[cascade] = lookAt(
            center - direction * lightDistance,
            center,
            up);
        result.projections[cascade] = orthographicProjection(
            -radius,
            radius,
            -radius,
            radius,
            0.1f,
            lightDistance + radius + depthPadding);
        result.projections[cascade][1][1] *= -1.0f;
        result.viewProjections[cascade] =
            result.projections[cascade] * result.views[cascade];
        cascadeNear = cascadeFar;
    }

    if (!cascadesEnabled) {
        for (size_t cascade = 1; cascade < ShadowCascadeCount; ++cascade) {
            result.views[cascade] = result.views[0];
            result.projections[cascade] = result.projections[0];
            result.viewProjections[cascade] = result.viewProjections[0];
        }
    }

    constexpr float pageScale =
        static_cast<float>(VirtualShadowPageResolution) /
        static_cast<float>(VirtualShadowAtlasResolution);
    for (size_t page = 0; page < ShadowCascadeCount; ++page) {
        result.virtualPageTable[page] = TSVec4f(
            static_cast<float>(page & 1u) * pageScale,
            static_cast<float>(page >> 1u) * pageScale,
            pageScale,
            pageScale);
    }

    return result;
}

} // namespace Tasrovy::Renderer
