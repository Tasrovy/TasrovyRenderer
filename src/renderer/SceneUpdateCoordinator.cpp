#include "SceneUpdateCoordinator.h"

#include "RenderScene.h"
#include "../render/Camera.h"
#include "../render/Light.h"
#include "../render/Mesh.h"
#include "../render/Object.h"
#include "../render/Scene.h"

#include <functional>
#include <unordered_map>

namespace {

using Tasrovy::Render::Object;
using Tasrovy::Render::Scene;

void indexObjects(
    const std::shared_ptr<Object>& object,
    std::unordered_map<uint64_t, Object*>& objects) {
    if (!object) return;
    objects[object->getRenderId()] = object.get();
    for (const auto& child : object->getChildren()) {
        indexObjects(child, objects);
    }
}

std::unordered_map<uint64_t, Object*> indexObjects(Scene& scene) {
    std::unordered_map<uint64_t, Object*> objects;
    for (const auto& object : scene.getObjects()) {
        indexObjects(object, objects);
    }
    return objects;
}

void applyObjectData(
    Scene& destination,
    const Scene& source,
    bool transforms,
    bool materials) {
    auto destinationObjects = indexObjects(destination);
    std::function<void(const std::shared_ptr<Object>&)> visit =
        [&](const auto& sourceObject) {
            if (!sourceObject) return;
            const auto found = destinationObjects.find(sourceObject->getRenderId());
            if (found != destinationObjects.end()) {
                auto& object = *found->second;
                if (transforms) {
                    object.setPosition(sourceObject->getPosition());
                    object.setRotation(sourceObject->getRotationQuat());
                    object.setScale(sourceObject->getScale());
                    object.setActive(sourceObject->isActive());
                    object.setFlipProjectionY(sourceObject->getFlipProjectionY());
                }
                if (materials) {
                    object.setMaterial(sourceObject->getMaterial());
                    if (const auto mesh = sourceObject->getMesh()) {
                        for (size_t index = 0;
                             index < mesh->getSubmeshes().size();
                             ++index) {
                            object.setSubmeshMaterial(
                                index, sourceObject->getSubmeshMaterial(index));
                        }
                    }
                }
            }
            for (const auto& child : sourceObject->getChildren()) visit(child);
        };
    for (const auto& object : source.getObjects()) visit(object);
}

void applyCameraData(Scene& destination, const Scene& source) {
    const auto* sourceCamera = source.getPrimaryCamera();
    auto* destinationCamera = destination.getPrimaryCamera();
    if (!sourceCamera || !destinationCamera) return;
    destinationCamera->setPosition(sourceCamera->getPosition());
    destinationCamera->setRotation(sourceCamera->getRotationQuat());
    destinationCamera->setFOV(sourceCamera->getFOV());
    destinationCamera->setAspect(sourceCamera->getAspect());
    destinationCamera->setNearPlane(sourceCamera->getNearPlane());
    destinationCamera->setFarPlane(sourceCamera->getFarPlane());
}

void applyLightingData(Scene& destination, const Scene& source) {
    while (destination.getLightCount() != 0) {
        destination.removeLight(destination.getLight(0));
    }
    for (const auto& light : source.getLights()) {
        if (light) destination.addLight(light->clone());
    }
}

} // namespace

namespace Tasrovy::Renderer {

SceneUpdateCoordinator::SceneUpdateCoordinator(RenderScene& renderScene)
    : renderScene_(renderScene) {}

SceneUpdateCoordinator::Update SceneUpdateCoordinator::synchronize(
    const PipelineEvaluator& evaluatePipeline) {
    const auto snapshot = renderScene_.snapshot();
    Update update{};
    update.pipeline = snapshot.pipeline;
    update.versions = snapshot.versions;
    if (!snapshot.scene) return update;

    update.structuralChanged = !activeScene_ ||
        snapshot.versions.structural != appliedVersions_.structural;
    update.transformChanged = !activeScene_ ||
        snapshot.versions.transform != appliedVersions_.transform;
    update.materialChanged = !activeScene_ ||
        snapshot.versions.material != appliedVersions_.material;
    update.lightingChanged = !activeScene_ ||
        snapshot.versions.lighting != appliedVersions_.lighting;
    update.pipelineChanged =
        snapshot.versions.pipeline != appliedVersions_.pipeline;

    const bool pipelineRequiresRebuild =
        evaluatePipeline ? evaluatePipeline(snapshot.pipeline) : false;
    if (update.structuralChanged) {
        activeScene_ = snapshot.scene->clone();
    } else {
        applyObjectData(
            *activeScene_, *snapshot.scene,
            update.transformChanged, update.materialChanged);
        if (update.transformChanged) {
            applyCameraData(*activeScene_, *snapshot.scene);
        }
        if (update.lightingChanged) {
            applyLightingData(*activeScene_, *snapshot.scene);
        }
    }
    update.rebuildRequired = update.structuralChanged ||
        update.pipelineChanged ||
        pipelineRequiresRebuild;
    update.scene = activeScene_;
    appliedVersions_ = snapshot.versions;
    return update;
}

std::shared_ptr<Tasrovy::Render::PipelineBase>
SceneUpdateCoordinator::currentPipeline() const {
    return renderScene_.snapshot().pipeline;
}

void SceneUpdateCoordinator::adoptPipelineIfEmpty(
    const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline) {
    renderScene_.adoptPipelineIfEmpty(pipeline);
}

} // namespace Tasrovy::Renderer
