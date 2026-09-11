#include "../RenderScene.h"
#include "../SceneUpdateCoordinator.h"
#include "../../render/Scene.h"
#include "../../render/Object.h"
#include "../../render/Light.h"

#include <iostream>

int main() {
    using Tasrovy::Render::Scene;
    using Tasrovy::Renderer::RenderScene;
    using Tasrovy::Renderer::SceneChange;
    using Tasrovy::Renderer::SceneUpdateCoordinator;

    RenderScene renderScene;
    auto source = Scene::create("Published A");
    auto sourceObject = Tasrovy::Render::Object::create("Stable Object");
    const auto stableRenderId = sourceObject->getRenderId();
    source->addObject(sourceObject);
    renderScene.submitScene(source);

    const auto first = renderScene.snapshot();
    if (!first.scene || first.scene->getName() != "Published A") {
        std::cerr << "Initial immutable scene was not published\n";
        return 1;
    }
    if (!first.scene->getObject(0) ||
        first.scene->getObject(0)->getRenderId() != stableRenderId) {
        std::cerr << "Render identity was not preserved by publication\n";
        return 6;
    }

    // The application-owned source was cloned at submission and cannot mutate
    // an already published render snapshot.
    source->setName("External mutation");
    if (first.scene->getName() != "Published A") {
        std::cerr << "External source mutated a published snapshot\n";
        return 2;
    }

    {
        auto state = renderScene.lock();
        state.scene()->setName("Published B");
        state.markChanged(SceneChange::Structural);
    }

    const auto second = renderScene.snapshot();
    if (!second.scene || second.scene->getName() != "Published B") {
        std::cerr << "Updated immutable scene was not published\n";
        return 3;
    }
    if (first.scene == second.scene ||
        first.scene->getName() != "Published A") {
        std::cerr << "A later edit mutated the previous snapshot\n";
        return 4;
    }
    if (second.versions.structural <= first.versions.structural) {
        std::cerr << "Structural scene generation was not advanced\n";
        return 5;
    }
    if (!second.scene->getObject(0) ||
        second.scene->getObject(0)->getRenderId() != stableRenderId) {
        std::cerr << "Render identity changed across snapshots\n";
        return 7;
    }

    SceneUpdateCoordinator coordinator(renderScene);
    const auto update = coordinator.synchronize(
        [](const auto&) { return false; });
    if (!update.scene || !update.rebuildRequired ||
        !update.structuralChanged ||
        update.scene->getName() != "Published B") {
        std::cerr << "Coordinator did not consume the structural update\n";
        return 8;
    }
    const auto stable = coordinator.synchronize(
        [](const auto&) { return false; });
    if (stable.rebuildRequired || stable.scene != update.scene ||
        stable.structuralChanged || stable.transformChanged ||
        stable.materialChanged || stable.lightingChanged ||
        stable.pipelineChanged) {
        std::cerr << "Coordinator rebuilt an unchanged scene\n";
        return 10;
    }

    auto transformSource = sourceObject->clone();
    transformSource->setPosition(Tasrovy::Base::TSVec3f(1.0f, 2.0f, 3.0f));
    const auto beforeTransform = renderScene.snapshot().versions;
    renderScene.updatePrimitive(*transformSource);
    const auto afterTransform = renderScene.snapshot().versions;
    if (afterTransform.transform <= beforeTransform.transform ||
        afterTransform.structural != beforeTransform.structural ||
        afterTransform.material != beforeTransform.material ||
        afterTransform.lighting != beforeTransform.lighting) {
        std::cerr << "Transform update advanced unrelated generations\n";
        return 11;
    }
    const auto transformUpdate = coordinator.synchronize(
        [](const auto&) { return false; });
    if (!transformUpdate.transformChanged ||
        transformUpdate.structuralChanged ||
        transformUpdate.rebuildRequired ||
        transformUpdate.scene != update.scene ||
        !transformUpdate.scene->getObject(0) ||
        transformUpdate.scene->getObject(0)->getPosition() !=
            Tasrovy::Base::TSVec3f(1.0f, 2.0f, 3.0f)) {
        std::cerr << "Transform update was not applied incrementally\n";
        return 12;
    }

    {
        auto state = renderScene.lock();
        state.scene()->addLight(Tasrovy::Render::DirectionalLight::create(
            Tasrovy::Base::TSVec3f(0.0f, -1.0f, 0.0f),
            Tasrovy::Base::TSVec3f(1.0f), 2.0f, "Test Light"));
        state.markChanged(SceneChange::Lighting);
    }
    const auto lightingUpdate = coordinator.synchronize(
        [](const auto&) { return false; });
    if (!lightingUpdate.lightingChanged ||
        lightingUpdate.structuralChanged ||
        lightingUpdate.rebuildRequired ||
        lightingUpdate.scene != transformUpdate.scene ||
        lightingUpdate.scene->getLightCount() != 1) {
        std::cerr << "Lighting update was not applied incrementally\n";
        return 13;
    }
    return 0;
}
