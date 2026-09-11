#include "RenderScene.h"

#include "../render/Pipeline.h"
#include "../render/Object.h"
#include "../render/Scene.h"

#include <utility>
#include <functional>

namespace Tasrovy::Renderer {

RenderScene::LockedState::LockedState(RenderScene& owner)
    : owner_(owner), lock_(owner.mutex_) {
}

std::shared_ptr<Tasrovy::Render::Scene>&
RenderScene::LockedState::scene() {
    return owner_.scene_;
}

std::shared_ptr<Tasrovy::Render::PipelineBase>&
RenderScene::LockedState::pipeline() {
    return owner_.pipeline_;
}

void RenderScene::LockedState::markChanged(SceneChange changes) {
    owner_.markChangedLocked(changes);
}

void RenderScene::submitScene(
    std::shared_ptr<Tasrovy::Render::Scene> scene) {
    std::lock_guard<std::mutex> guard(mutex_);
    scene_ = scene ? scene->clone() : nullptr;
    rebuildProxiesLocked();
    markChangedLocked(SceneChange::AllScene);
}

void RenderScene::addPrimitive(
    const Tasrovy::Render::Object& object) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!scene_) {
        return;
    }
    auto clone = object.clone();
    const auto proxy = PrimitiveSceneProxy::fromObject(*clone);
    scene_->addObject(std::move(clone));
    proxies_[proxy.id] = proxy;
    markChangedLocked(SceneChange::Structural);
}

void RenderScene::updatePrimitive(
    const Tasrovy::Render::Object& object) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto proxy = PrimitiveSceneProxy::fromObject(object);
    SceneChange changes = SceneChange::None;
    const auto previous = proxies_.find(proxy.id);
    if (previous == proxies_.end() ||
        previous->second.mesh != proxy.mesh ||
        previous->second.active != proxy.active) {
        changes |= SceneChange::Structural;
    }
    if (previous == proxies_.end() ||
        previous->second.position != proxy.position ||
        previous->second.rotation != proxy.rotation ||
        previous->second.scale != proxy.scale ||
        previous->second.flipProjectionY != proxy.flipProjectionY) {
        changes |= SceneChange::Transform;
    }
    if (previous == proxies_.end() ||
        previous->second.material != proxy.material ||
        previous->second.submeshMaterials != proxy.submeshMaterials) {
        // Replacing a material can change descriptor resources, shader
        // permutations and surface routing. Parameter-only edits use the
        // Material generation without Structural.
        changes |= SceneChange::Material | SceneChange::Structural;
    }
    if (changes == SceneChange::None) {
        return;
    }
    proxies_[proxy.id] = proxy;
    applyProxyLocked(proxy);
    markChangedLocked(changes);
}

void RenderScene::removePrimitive(const std::string& name) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!scene_) {
        return;
    }
    const auto found = scene_->findObject(name);
    if (!found) {
        return;
    }
    const auto id = PrimitiveSceneProxy::fromObject(*found).id;
    scene_->removeObject(found);
    proxies_.erase(id);
    markChangedLocked(SceneChange::Structural);
}

void RenderScene::submitPipeline(
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline) {
    std::lock_guard<std::mutex> guard(mutex_);
    pipeline_ = std::move(pipeline);
    markChangedLocked(SceneChange::Pipeline);
}

RenderScene::Snapshot RenderScene::snapshot() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return {
        publishedScene_,
        pipeline_,
        versions_
    };
}

RenderScene::LockedState RenderScene::lock() {
    return LockedState(*this);
}

void RenderScene::adoptPipelineIfEmpty(
    const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!pipeline_) {
        pipeline_ = pipeline;
        ++versions_.pipeline;
    }
}

void RenderScene::markChangedLocked(SceneChange changes) {
    if (changes == SceneChange::None) {
        return;
    }
    if (hasSceneChange(changes, SceneChange::Structural) ||
        hasSceneChange(changes, SceneChange::Transform) ||
        hasSceneChange(changes, SceneChange::Material) ||
        hasSceneChange(changes, SceneChange::Lighting)) {
        publishSceneLocked();
    }
    if (hasSceneChange(changes, SceneChange::Structural)) ++versions_.structural;
    if (hasSceneChange(changes, SceneChange::Transform)) ++versions_.transform;
    if (hasSceneChange(changes, SceneChange::Material)) ++versions_.material;
    if (hasSceneChange(changes, SceneChange::Lighting)) ++versions_.lighting;
    if (hasSceneChange(changes, SceneChange::Pipeline)) ++versions_.pipeline;
}

void RenderScene::publishSceneLocked() {
    publishedScene_ = scene_ ? scene_->clone() : nullptr;
}

void RenderScene::rebuildProxiesLocked() {
    proxies_.clear();
    if (!scene_) {
        return;
    }
    std::function<void(const std::shared_ptr<Tasrovy::Render::Object>&)>
        visit = [&](const auto& object) {
            if (!object) {
                return;
            }
            auto proxy = PrimitiveSceneProxy::fromObject(*object);
            proxies_[proxy.id] = std::move(proxy);
            for (const auto& child : object->getChildren()) {
                visit(child);
            }
        };
    for (const auto& object : scene_->getObjects()) {
        visit(object);
    }
}

void RenderScene::applyProxyLocked(
    const PrimitiveSceneProxy& proxy) {
    if (!scene_) {
        return;
    }
    auto* object = scene_->findObject(proxy.name);
    if (!object) {
        return;
    }
    object->setPosition(proxy.position);
    object->setRotation(proxy.rotation);
    object->setScale(proxy.scale);
    object->setMesh(proxy.mesh);
    object->setMaterial(proxy.material);
    object->setActive(proxy.active);
    object->setFlipProjectionY(proxy.flipProjectionY);
    for (size_t index = 0;
         index < proxy.submeshMaterials.size();
         ++index) {
        object->setSubmeshMaterial(
            index, proxy.submeshMaterials[index]);
    }
}

} // namespace Tasrovy::Renderer
