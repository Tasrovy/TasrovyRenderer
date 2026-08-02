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

void RenderScene::LockedState::markDirty() {
    owner_.markDirtyLocked();
}

void RenderScene::submitScene(
    std::shared_ptr<Tasrovy::Render::Scene> scene) {
    std::lock_guard<std::mutex> guard(mutex_);
    scene_ = scene ? scene->clone() : nullptr;
    rebuildProxiesLocked();
    markDirtyLocked();
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
    markDirtyLocked();
}

void RenderScene::updatePrimitive(
    const Tasrovy::Render::Object& object) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto proxy = PrimitiveSceneProxy::fromObject(object);
    proxies_[proxy.id] = proxy;
    applyProxyLocked(proxy);
    markDirtyLocked();
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
    markDirtyLocked();
}

void RenderScene::submitPipeline(
    std::shared_ptr<Tasrovy::Render::PipelineBase> pipeline) {
    std::lock_guard<std::mutex> guard(mutex_);
    pipeline_ = std::move(pipeline);
    markDirtyLocked();
}

RenderScene::Snapshot RenderScene::snapshot() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return {
        scene_,
        pipeline_,
        dirty_,
        version_
    };
}

RenderScene::LockedState RenderScene::lock() {
    return LockedState(*this);
}

void RenderScene::acknowledge(uint64_t version) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (version_ == version) {
        dirty_ = false;
    }
}

void RenderScene::adoptPipelineIfEmpty(
    const std::shared_ptr<Tasrovy::Render::PipelineBase>& pipeline) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!pipeline_) {
        pipeline_ = pipeline;
    }
}

void RenderScene::markDirtyLocked() {
    dirty_ = true;
    ++version_;
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
