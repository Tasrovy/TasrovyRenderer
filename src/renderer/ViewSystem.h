#pragma once

#include "ViewState.h"

#include "TSMatrix.h"
#include "TSQuaternion.h"
#include "TSVector.h"

#include <cstdint>
#include <unordered_map>

namespace Tasrovy::Render {
class Camera;
class Object;
}

namespace Tasrovy::Renderer {

struct ViewFrameData {
    Tasrovy::Base::TSMat4f view = Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSMat4f flippedProjection = Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSMat4f unflippedProjection = Tasrovy::Base::TSMat4f(1.0f);
    Tasrovy::Base::TSVec2f jitterUv = Tasrovy::Base::TSVec2f(0.0f);
    Tasrovy::Base::TSVec2f jitterDeltaUv = Tasrovy::Base::TSVec2f(0.0f);
    Tasrovy::Base::TSVec3f cameraPosition = Tasrovy::Base::TSVec3f(0.0f);
    Tasrovy::Base::TSQuatf cameraRotation =
        Tasrovy::Base::TSQuatf(1.0f, 0.0f, 0.0f, 0.0f);
    float cameraFov = 0.0f;
    bool cameraCut = false;
};

// Owns the temporal-view policy while ViewState remains the persistent data
// container. Keeping this API independent of RHI makes it usable per camera or
// viewport when SceneRenderer gains multiple views.
class ViewSystem {
public:
    ViewFrameData beginFrame(
        const Tasrovy::Render::Camera& camera,
        ViewState& state,
        bool temporalAAEnabled,
        uint32_t internalWidth,
        uint32_t internalHeight) const;

    void commitFrame(
        ViewState& state,
        const ViewFrameData& frame,
        std::unordered_map<
            const Tasrovy::Render::Object*,
            Tasrovy::Base::TSMat4f>&& currentModelMatrices,
        bool temporalAAEnabled) const;
};

} // namespace Tasrovy::Renderer
