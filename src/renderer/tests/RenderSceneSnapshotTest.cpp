#include "../RenderScene.h"
#include "../../render/Scene.h"
#include "../../render/Object.h"

#include <iostream>

int main() {
    using Tasrovy::Render::Scene;
    using Tasrovy::Renderer::RenderScene;

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
        state.markDirty();
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
    if (second.version <= first.version || !second.dirty) {
        std::cerr << "Published snapshot version was not advanced\n";
        return 5;
    }
    if (!second.scene->getObject(0) ||
        second.scene->getObject(0)->getRenderId() != stableRenderId) {
        std::cerr << "Render identity changed across snapshots\n";
        return 7;
    }
    return 0;
}
