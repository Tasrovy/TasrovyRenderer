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
#include "UVDebugMesh.h"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace Tasrovy;
using namespace Tasrovy::Render;

namespace {

constexpr const char* kMainModelPath = "res/Models/Taffy/Taffy.obj";

std::shared_ptr<Material> createPbrMaterial(const std::string& baseColorPath)
{
    auto material = Material::create();
    material->setTexture(MaterialTextureSemantic::BaseColor, 1, baseColorPath);
    material->setTexture(MaterialTextureSemantic::Normal, 2, "res\\Textures\\Taffy\\neutral_normal.png");
    material->setTexture(MaterialTextureSemantic::Emissive, 3, "res\\Textures\\Taffy\\neutral_emissive.png");
    material->setTexture(MaterialTextureSemantic::MetallicRoughnessAO, 4, "res\\Textures\\Taffy\\neutral_mra.png");
    return material;
}

std::string chooseTaffyBaseColor(const std::string& materialName)
{
    if (materialName.find("Hair") != std::string::npos) {
        return "res\\Textures\\Taffy\\hair.png";
    }
    if (materialName.find("Face") != std::string::npos ||
        materialName.find("Eye") != std::string::npos) {
        return "res\\Textures\\Taffy\\face.png";
    }
    return "res\\Textures\\Taffy\\cloth.png";
}

void setWorkingDirectoryToAssetRoot()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    if (ec) {
        LOG_WARN("Failed to query current working directory: {}", ec.message());
        return;
    }

    while (!dir.empty()) {
        if (fs::exists(dir / kMainModelPath, ec) && !ec) {
            fs::current_path(dir, ec);
            if (ec) {
                LOG_WARN("Failed to switch working directory to '{}': {}", dir.string(), ec.message());
            } else {
                LOG_INFO("Working directory set to asset root '{}'", dir.string());
            }
            return;
        }

        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    LOG_WARN("Could not locate asset root containing {} from '{}'", kMainModelPath, fs::current_path().string());
}

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
    setWorkingDirectoryToAssetRoot();

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
    LOG_INFO("Main scene using placeholder cube while {} loads on filesystem thread", kMainModelPath);

    auto material = createPbrMaterial("res\\Textures\\Taffy\\cloth.png");
    auto uvDebugObj = Object::create("UVUnwrapDebug");
    std::shared_ptr<Mesh> uvDebugMesh = UVDebugMesh::createFromMesh(*mainMesh);
    std::vector<std::shared_ptr<Material>> mainSubmeshMaterials;
    std::deque<std::shared_ptr<Mesh>> retiredMeshes;
    std::deque<std::vector<std::shared_ptr<Material>>> retiredSubmeshMaterials;
    constexpr size_t maxRetiredSceneResources = 8;
    mainObj->setMesh(mainMesh);
    mainObj->setMaterial(material);
    scene->addObject(mainObj);
    uvDebugObj->setMesh(uvDebugMesh);
    uvDebugObj->setMaterial(material);
    uvDebugObj->setPosition(TSVec3f(3.0f, 0.0f, 0.0f));
    uvDebugObj->setScale(TSVec3f(0.65f));
    scene->addObject(uvDebugObj);

    auto skybox = Skybox::create("MainSkybox");
    auto skyTex = Texture::createCubemap("res/Skyboxes/PurpleSky");
    skybox->setCubemap(std::move(skyTex));
    scene->addObject(std::move(skybox));

    auto pipeline = PBRPipeline::create();
    pipeline->GenPass(scene);

    Tasrovy::FS::AssetManager assets(4);
    assets.requestModel(kMainModelPath);
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
            if (!modelApplied && event->kind == Tasrovy::FS::AssetKind::Model && event->path == kMainModelPath) {
                modelApplied = true;
                if (event->success) {
                    auto model = assets.getModel(event->path);
                    auto loadedMesh = model ? Mesh::fromModel(*model) : nullptr;
                    if (!loadedMesh) {
                        LOG_WARN("Async model load completed but no model cache entry was found '{}'", event->path);
                        continue;
                    }
                    std::vector<std::shared_ptr<Material>> loadedSubmeshMaterials;
                    const auto& submeshes = loadedMesh->getSubmeshes();
                    loadedSubmeshMaterials.reserve(submeshes.size());
                    for (const auto& submesh : submeshes) {
                        loadedSubmeshMaterials.push_back(
                            createPbrMaterial(chooseTaffyBaseColor(submesh.getMaterialName())));
                    }

                    mainObj->setMesh(loadedMesh);
                    mainObj->clearSubmeshMaterials();
                    for (size_t i = 0; i < loadedSubmeshMaterials.size(); ++i) {
                        loadedMesh->setSubmeshMaterial(i, loadedSubmeshMaterials[i]);
                    }
                    auto loadedUvDebugMesh = UVDebugMesh::createFromMesh(*loadedMesh);
                    for (size_t i = 0; i < loadedSubmeshMaterials.size(); ++i) {
                        loadedUvDebugMesh->setSubmeshMaterial(i, loadedSubmeshMaterials[i]);
                    }
                    uvDebugObj->setMesh(loadedUvDebugMesh);
                    uvDebugObj->setMaterial(loadedSubmeshMaterials.empty() ? material : loadedSubmeshMaterials.front());
                    fitObjectToMeshBounds(mainObj, loadedMesh);
                    renderer.setScene(scene);

                    if (mainMesh) {
                        retiredMeshes.push_back(mainMesh);
                    }
                    if (uvDebugMesh) {
                        retiredMeshes.push_back(uvDebugMesh);
                    }
                    if (!mainSubmeshMaterials.empty()) {
                        retiredSubmeshMaterials.push_back(std::move(mainSubmeshMaterials));
                    }
                    mainMesh = std::move(loadedMesh);
                    uvDebugMesh = std::move(loadedUvDebugMesh);
                    mainSubmeshMaterials = std::move(loadedSubmeshMaterials);
                    while (retiredMeshes.size() > maxRetiredSceneResources) {
                        retiredMeshes.pop_front();
                    }
                    while (retiredSubmeshMaterials.size() > maxRetiredSceneResources) {
                        retiredSubmeshMaterials.pop_front();
                    }
                    LOG_INFO(
                        "Async model applied '{}': {} vertices, {} indices, {} submeshes",
                        event->path,
                        mainMesh->getVertexCount(),
                        mainMesh->getIndexCount(),
                        submeshes.size());
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
