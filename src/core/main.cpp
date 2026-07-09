#include <volk.h>
#include <Logger.hpp>
#include <Window.h>
#include <SceneRenderer.h>
#include "AssetManager.hpp"
#include "Scene.h"
#include "Camera.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Model.hpp"
#include "Object.h"
#include "Pipeline.h"
#include "PipelinePass.h"
#include "PBRPipeline.h"
#include "Skybox.h"
#include "Texture.hpp"
#include <algorithm>
#include <limits>

using namespace Tasrovy;
using namespace Tasrovy::Render;

namespace {

void fitObjectToMeshBounds(const std::shared_ptr<Object>& object, const std::shared_ptr<Mesh>& mesh)
{
    if (!object || !mesh || mesh->getVertices().empty()) {
        return;
    }

    TSVec3f minBounds(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    TSVec3f maxBounds(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());

    for (const auto& vertex : mesh->getVertices()) {
        minBounds.x = std::min(minBounds.x, vertex.position.x);
        minBounds.y = std::min(minBounds.y, vertex.position.y);
        minBounds.z = std::min(minBounds.z, vertex.position.z);
        maxBounds.x = std::max(maxBounds.x, vertex.position.x);
        maxBounds.y = std::max(maxBounds.y, vertex.position.y);
        maxBounds.z = std::max(maxBounds.z, vertex.position.z);
    }

    const TSVec3f size = maxBounds - minBounds;
    const TSVec3f center = (minBounds + maxBounds) * 0.5f;
    const float maxExtent = std::max(size.x, std::max(size.y, size.z));
    const float scaleValue = maxExtent > 0.0f ? 2.0f / maxExtent : 1.0f;

    object->setScale(TSVec3f(scaleValue));
    object->setPosition(center * -scaleValue);

    LOG_INFO(
        "Fit model bounds center ({:.3f}, {:.3f}, {:.3f}) size ({:.3f}, {:.3f}, {:.3f}) scale {:.5f}",
        center.x,
        center.y,
        center.z,
        size.x,
        size.y,
        size.z,
        scaleValue);
}

} // namespace

int main()
{
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("Failed to initialize volk!");
        return -1;
    }
    Tasrovy::Log::Logger::Init();

    // ======== Create window ========
    Tasrovy::Windowing::Window window(1280, 800, "TasrovyRenderer");

    // ======== Build scene ========
    auto scene = Scene::create("MainScene");

    auto camera = Camera::create(
        TSVec3f(0.0f, 1.0f, 5.0f), TSVec3f(0.0f),
        45.0f, (float)window.getWidth() / (float)window.getHeight(),
        0.1f, 100.0f, "MainCamera");
    scene->addCamera(std::move(camera));
    scene->setPrimaryCamera(scene->getCameras().back().get());

    auto dirLight = DirectionalLight::create(
        TSVec3f(-0.5f, -1.0f, -0.8f), TSVec3f(1.0f), 10.0f, "Sun");
    scene->addLight(std::move(dirLight));

    auto mainObj = Object::create("MainModel");
    auto cubeModel = Tasrovy::FS::Model::GenCube();
    std::shared_ptr<Mesh> mainMesh = Mesh::fromModel(*cubeModel);
    LOG_INFO("Main scene using placeholder cube while res/model.obj loads on filesystem thread");

    auto material = Material::create();
    material->setTexture(MaterialTextureSemantic::BaseColor, 1, "res\\diffuse.png");
    material->setTexture(MaterialTextureSemantic::Normal, 2, "res\\normal.png");
    material->setTexture(MaterialTextureSemantic::Emissive, 3, "res\\emissive.png");
    material->setTexture(MaterialTextureSemantic::MetallicRoughnessAO, 4, "res\\msa.png");
    material->setFloat("uMetallic", 1.0f);
    material->setFloat("uRoughness", 1.0f);
    material->setFloat("uAo", 1.0f);
    mainObj->setMesh(mainMesh);
    mainObj->setMaterial(material);
    scene->addObject(mainObj);

    auto skybox = Skybox::create("MainSkybox");
    auto skyTex = Texture::createCubemap("res/PurpleSky");
    skybox->setCubemap(std::move(skyTex));
    scene->addObject(std::move(skybox));

    auto pipeline = PBRPipeline::create();
    pipeline->GenPass(scene);

    Tasrovy::FS::AssetManager assets(4);
    assets.requestModel("res/model.obj");
    bool modelApplied = false;

    // ======== Submit to RHI ========
    Tasrovy::RHI::SceneRenderer renderer(window, 4);
    renderer.setScene(scene);
    renderer.setPipeline(pipeline);
    renderer.start();

    LOG_INFO("RHI render thread started, main thread handling window events");

    // ======== Main loop — just handle input ========
    while (!window.shouldClose()) {
        window.pollEvents();

        while (auto event = assets.pollEvent()) {
            if (!modelApplied && event->kind == Tasrovy::FS::AssetKind::Model && event->path == "res/model.obj") {
                modelApplied = true;
                if (event->success) {
                    auto model = assets.getModel(event->path);
                    mainMesh = model ? Mesh::fromModel(*model) : nullptr;
                    if (!mainMesh) {
                        LOG_WARN("Async model load completed but no model cache entry was found '{}'", event->path);
                        continue;
                    }
                    mainObj->setMesh(mainMesh);
                    fitObjectToMeshBounds(mainObj, mainMesh);
                    renderer.setScene(scene);
                    LOG_INFO(
                        "Async model applied '{}': {} vertices, {} indices",
                        event->path,
                        mainMesh->getVertexCount(),
                        mainMesh->getIndexCount());
                } else {
                    LOG_WARN(
                        "Async model load failed '{}': {}",
                        event->path,
                        event->error.empty() ? "unknown error" : event->error);
                }
            }
        }
    }

    renderer.stop();
    LOG_INFO("Application exiting");
    return 0;
}
