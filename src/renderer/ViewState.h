#pragma once

#include "TSMatrix.h"
#include "TSQuaternion.h"
#include "TSVector.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace Tasrovy::Render {
class Object;
}

namespace Tasrovy::Renderer {

// Persistent state associated with a view. SceneRenderer instances orchestrate
// frames; temporal history belongs here so it can survive graph rebuilds and
// can later be owned per camera / viewport, like Unreal's FSceneViewState.
struct ViewState {
    bool temporalHistoryValid = false;
    uint64_t temporalFrameIndex = 0;
    Tasrovy::Base::TSMat4f previousView =
        Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSMat4f previousFlippedProjection =
        Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSMat4f previousUnflippedProjection =
        Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSVec2f previousJitterUv =
        Tasrovy::Base::TSVec2f(0.0f);
    Tasrovy::Base::TSVec3f previousCameraPosition =
        Tasrovy::Base::TSVec3f(0.0f);
    Tasrovy::Base::TSQuatf previousCameraRotation =
        Tasrovy::Base::TSQuatf(1.0f, 0.0f, 0.0f, 0.0f);
    float previousCameraFov = 0.0f;
    bool cameraInitialized = false;
    std::string historyStatus = "Not initialized";
    std::unordered_map<
        const Tasrovy::Render::Object*,
        Tasrovy::Base::TSMat4f> previousModelMatrices;

    void invalidate(std::string reason, bool resetFrameIndex = false) {
        temporalHistoryValid = false;
        previousJitterUv = Tasrovy::Base::TSVec2f(0.0f);
        cameraInitialized = false;
        previousModelMatrices.clear();
        historyStatus = std::move(reason);
        if (resetFrameIndex) {
            temporalFrameIndex = 0;
        }
    }
};

} // namespace Tasrovy::Renderer
