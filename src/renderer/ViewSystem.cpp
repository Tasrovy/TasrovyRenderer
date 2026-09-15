#include "ViewSystem.h"

#include "../render/Camera.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Tasrovy::Renderer {

using namespace Tasrovy::Base;

namespace {

float halton(uint64_t index, uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

bool isCameraCut(
    const Tasrovy::Render::Camera& camera,
    const ViewState& state) {
    if (!state.cameraInitialized || !state.temporalHistoryValid) {
        return false;
    }

    const TSVec3f cameraPosition = camera.getPosition();
    const TSQuatf cameraRotation = camera.getRotationQuat();
    const float positionDelta = length(
        cameraPosition - state.previousCameraPosition);
    const float quaternionDot = std::abs(
        cameraRotation.w * state.previousCameraRotation.w +
        cameraRotation.x * state.previousCameraRotation.x +
        cameraRotation.y * state.previousCameraRotation.y +
        cameraRotation.z * state.previousCameraRotation.z);
    const float rotationDelta = 2.0f * std::acos(
        std::clamp(quaternionDot, 0.0f, 1.0f));
    const float fovDelta = std::abs(
        camera.getFOV() - state.previousCameraFov);
    return positionDelta > 2.0f ||
        rotationDelta > radians(45.0f) ||
        fovDelta > 1.0f;
}

} // namespace

ViewFrameData ViewSystem::beginFrame(
    const Tasrovy::Render::Camera& camera,
    ViewState& state,
    bool temporalAAEnabled,
    uint32_t internalWidth,
    uint32_t internalHeight,
    uint32_t displayWidth,
    uint32_t displayHeight) const {
    ViewFrameData frame;
    frame.cameraPosition = camera.getPosition();
    frame.cameraRotation = camera.getRotationQuat();
    frame.cameraFov = camera.getFOV();
    frame.cameraCut = isCameraCut(camera, state);
    if (frame.cameraCut) {
        state.temporalHistoryValid = false;
        state.previousModelMatrices.clear();
        state.historyStatus = "Camera cut";
    }

    frame.view = camera.getViewMatrix();
    frame.unflippedProjection = camera.getProjectionMatrix();
    if (temporalAAEnabled) {
        const uint32_t safeInternalWidth = std::max(internalWidth, 1u);
        const uint32_t safeInternalHeight = std::max(internalHeight, 1u);
        const uint32_t phasesX = std::max(
            1u, (std::max(displayWidth, 1u) + safeInternalWidth - 1u) /
                safeInternalWidth);
        const uint32_t phasesY = std::max(
            1u, (std::max(displayHeight, 1u) + safeInternalHeight - 1u) /
                safeInternalHeight);
        // Native TAA keeps the established eight-sample sequence. TAAU needs
        // enough phases to cover the display pixels represented by one
        // internal pixel (for example 4x4 phases at 25% resolution).
        const uint64_t jitterPhaseCount = std::clamp<uint64_t>(
            static_cast<uint64_t>(phasesX) * phasesY, 8u, 64u);
        const uint64_t jitterIndex =
            state.temporalFrameIndex % jitterPhaseCount + 1u;
        const float jitterX = halton(jitterIndex, 2u) - 0.5f;
        const float jitterY = halton(jitterIndex, 3u) - 0.5f;
        frame.jitterUv = TSVec2f(
            jitterX / static_cast<float>(safeInternalWidth),
            jitterY / static_cast<float>(safeInternalHeight));
        // Projection[2][0/1] contributes with the opposite sign after the
        // perspective divide. Subtract here so jitterUv consistently means
        // the actual screen-UV displacement used by velocity consumers.
        frame.unflippedProjection[2][0] -= frame.jitterUv.x * 2.0f;
        frame.unflippedProjection[2][1] -= frame.jitterUv.y * 2.0f;
    }

    frame.jitterDeltaUv = state.temporalHistoryValid
        ? frame.jitterUv - state.previousJitterUv
        : TSVec2f(0.0f);
    frame.flippedProjection = frame.unflippedProjection;
    frame.flippedProjection[1][1] *= -1.0f;
    return frame;
}

void ViewSystem::commitFrame(
    ViewState& state,
    const ViewFrameData& frame,
    std::unordered_map<
        const Tasrovy::Render::Object*,
        TSMat4f>&& currentModelMatrices,
    bool temporalAAEnabled) const {
    state.previousView = frame.view;
    state.previousFlippedProjection = frame.flippedProjection;
    state.previousUnflippedProjection = frame.unflippedProjection;
    state.previousJitterUv = frame.jitterUv;
    state.previousCameraPosition = frame.cameraPosition;
    state.previousCameraRotation = frame.cameraRotation;
    state.previousCameraFov = frame.cameraFov;
    state.cameraInitialized = true;
    state.previousModelMatrices = std::move(currentModelMatrices);
    state.temporalHistoryValid = true;
    if (frame.cameraCut) {
        state.historyStatus = "Camera cut; history restarted";
    } else if (temporalAAEnabled) {
        state.historyStatus = "Accumulating";
    } else {
        state.historyStatus = "Disabled";
    }
    ++state.temporalFrameIndex;
}

} // namespace Tasrovy::Renderer
