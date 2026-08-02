#include "SceneAnimationSystem.h"

#include "SceneRendererComponents.h"
#include "../render/Object.h"
#include "../render/Scene.h"
#include "TSVector.h"

#include <algorithm>
#include <cmath>

namespace Tasrovy::Renderer {

void SceneAnimationSystem::update(
    Tasrovy::Render::Scene& scene,
    SceneRendererComponents& state,
    std::chrono::steady_clock::time_point now) {
    using namespace Tasrovy::Base;
    auto* taffy = scene.findObject("Taffy");
    auto* stressTaffy = scene.findObject("Taffy_0");
    auto* anchor = taffy ? taffy : stressTaffy;
    if (anchor != state.animatedTaffy) {
        state.animatedTaffy = anchor;
        state.taffyBaseRotation = anchor
            ? anchor->getRotationEuler()
            : TSVec3f(0.0f);
        state.taffyYawOffset = 0.0f;
        state.lastTaffyAnimationTime = now;
        return;
    }
    if (!anchor) return;
    if (!state.settings.taffyRotationEnabled) {
        state.taffyBaseRotation = anchor->getRotationEuler();
        state.taffyYawOffset = 0.0f;
        state.lastTaffyAnimationTime = now;
        return;
    }

    const float deltaSeconds = std::clamp(
        std::chrono::duration<float>(
            now - state.lastTaffyAnimationTime).count(),
        0.0f,
        0.1f);
    state.taffyYawOffset = std::fmod(
        state.taffyYawOffset + pi<float>() * 0.25f * deltaSeconds,
        2.0f * pi<float>());
    auto rotation = state.taffyBaseRotation;
    rotation.y += state.taffyYawOffset;
    if (taffy) {
        taffy->setRotation(rotation);
    } else {
        for (const auto& object : scene.getObjects()) {
            if (object && object->getName().starts_with("Taffy_")) {
                object->setRotation(rotation);
            }
        }
    }
    state.lastTaffyAnimationTime = now;
}

} // namespace Tasrovy::Renderer
