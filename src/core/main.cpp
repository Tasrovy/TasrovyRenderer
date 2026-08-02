#include <Logger.hpp>
#include <SceneRenderer.h>
#include <RenderAssetFactory.h>
#include <Window.h>

#include "Camera.h"
#include "DeferredPipeline.h"
#include "AssetLoader.hpp"
#include "Light.h"
#include "Material.h"
#include "MaterialDescriptor.h"
#include "Mesh.h"
#include "Object.h"
#include "Primitive.h"
#include "Scene.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Tasrovy;
using namespace Tasrovy::Render;

namespace {

void setWorkingDirectoryToProjectRoot()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path directory = fs::current_path(ec);
    if (ec) {
        LOG_WARN("Failed to query current working directory: {}", ec.message());
        return;
    }

    while (!directory.empty()) {
        if (fs::exists(directory / "res" / "Shaders", ec) && !ec) {
            fs::current_path(directory, ec);
            if (ec) {
                LOG_WARN("Failed to switch working directory to '{}': {}", directory.string(), ec.message());
            } else {
                LOG_INFO("Working directory set to project root '{}'", directory.string());
            }
            return;
        }

        const fs::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }

    LOG_WARN("Could not locate the project root from '{}'", fs::current_path().string());
}

std::shared_ptr<Material> loadMaterial(const std::string& descriptorPath)
{
    return Material::create(MaterialDescriptor::load(descriptorPath));
}

void placeModelOnFloor(
    const std::shared_ptr<Object>& object,
    const std::shared_ptr<Mesh>& mesh,
    float targetHeight,
    float centerZ)
{
    if (!object || !mesh || mesh->getVertices().empty()) {
        return;
    }

    TSVec3f minimum(std::numeric_limits<float>::max());
    TSVec3f maximum(std::numeric_limits<float>::lowest());
    for (const auto& vertex : mesh->getVertices()) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }

    const float sourceHeight = maximum.y - minimum.y;
    const float uniformScale = sourceHeight > 0.0f ? targetHeight / sourceHeight : 1.0f;
    const TSVec3f center = (minimum + maximum) * 0.5f;
    object->setScale(TSVec3f(uniformScale));
    object->setPosition(TSVec3f(
        -center.x * uniformScale,
        -minimum.y * uniformScale,
        centerZ - center.z * uniformScale));

    LOG_INFO(
        "Placed Taffy bounds min ({:.3f}, {:.3f}, {:.3f}) max ({:.3f}, {:.3f}, {:.3f}) scale {:.5f}",
        minimum.x,
        minimum.y,
        minimum.z,
        maximum.x,
        maximum.y,
        maximum.z,
        uniformScale);
}

template <typename PrimitiveType>
std::shared_ptr<PrimitiveType> addPrimitive(
    const std::shared_ptr<Scene>& scene,
    const std::string& name,
    const std::shared_ptr<Material>& material,
    const TSVec3f& position,
    const TSVec3f& rotation,
    const TSVec3f& scale)
{
    auto object = PrimitiveType::create(name);
    object->setMaterial(material);
    object->setPosition(position);
    object->setRotation(rotation);
    object->setScale(scale);
    scene->addObject(object);
    return object;
}

std::shared_ptr<Scene> createCornellBoxScene(
    float aspectRatio,
    std::vector<std::shared_ptr<Material>>& sceneMaterials,
    std::vector<std::shared_ptr<Mesh>>& sceneMeshes)
{
    auto scene = Scene::create("CornellBox");

    auto camera = Camera::create(
        TSVec3f(0.0f, 2.5f, 8.0f),
        TSVec3f(0.0f),
        42.0f,
        aspectRatio,
        0.1f,
        100.0f,
        "CornellCamera");
    Camera* cameraPtr = camera.get();
    scene->addCamera(std::move(camera));
    scene->setPrimaryCamera(cameraPtr);

    // Values are linear RGB because all lighting and G-buffer calculations
    // remain in linear space. The final presentation pass performs encoding.
    const auto floorMaterial =
        loadMaterial("res/Materials/Cornell/Floor.material.json");
    const auto ceilingMaterial =
        loadMaterial("res/Materials/Cornell/Ceiling.material.json");
    const auto backWallMaterial =
        loadMaterial("res/Materials/Cornell/BackWall.material.json");
    const auto leftWallMaterial =
        loadMaterial("res/Materials/Cornell/LeftWall.material.json");
    const auto rightWallMaterial =
        loadMaterial("res/Materials/Cornell/RightWall.material.json");
    const auto lightPanel =
        loadMaterial("res/Materials/Cornell/AreaLightPanel.material.json");
    sceneMaterials = {
        floorMaterial,
        ceilingMaterial,
        backWallMaterial,
        leftWallMaterial,
        rightWallMaterial,
        lightPanel
    };

    // Plane is authored in XZ with a +Y front face. Rotate each wall so the
    // front face points into the room; normal back-face culling then works.
    addPrimitive<Plane>(scene, "Floor", floorMaterial,
        TSVec3f(0.0f, 0.0f, 0.0f), TSVec3f(0.0f), TSVec3f(5.0f, 1.0f, 5.0f));
    addPrimitive<Plane>(scene, "Ceiling", ceilingMaterial,
        TSVec3f(0.0f, 5.0f, 0.0f), TSVec3f(pi<float>(), 0.0f, 0.0f), TSVec3f(5.0f, 1.0f, 5.0f));
    addPrimitive<Plane>(scene, "BackWall", backWallMaterial,
        TSVec3f(0.0f, 2.5f, -2.5f), TSVec3f(pi<float>() * 0.5f, 0.0f, 0.0f), TSVec3f(5.0f, 1.0f, 5.0f));
    addPrimitive<Plane>(scene, "LeftWall", leftWallMaterial,
        TSVec3f(-2.5f, 2.5f, 0.0f), TSVec3f(0.0f, 0.0f, -pi<float>() * 0.5f), TSVec3f(5.0f, 1.0f, 5.0f));
    addPrimitive<Plane>(scene, "RightWall", rightWallMaterial,
        TSVec3f(2.5f, 2.5f, 0.0f), TSVec3f(0.0f, 0.0f, pi<float>() * 0.5f), TSVec3f(5.0f, 1.0f, 5.0f));

    addPrimitive<Plane>(scene, "AreaLightPanel", lightPanel,
        TSVec3f(0.0f, 4.96f, -0.25f), TSVec3f(pi<float>(), 0.0f, 0.0f), TSVec3f(1.5f, 1.0f, 1.0f));

    Tasrovy::FS::AssetLoader assetLoader;
    const auto taffyModel = assetLoader.LoadModel("res/Models/Taffy/Taffy.obj");
    if (taffyModel) {
        auto taffyMesh =
            Tasrovy::Assets::RenderAssetFactory::meshFromModel(*taffyModel);
        taffyMesh->setSourcePath("res/Models/Taffy/Taffy.obj");
        auto taffy = Object::create("Taffy");
        const auto bodyMaterial =
            loadMaterial("res/Materials/Taffy/Body.material.json");
        const auto faceMaterial =
            loadMaterial("res/Materials/Taffy/Face.material.json");
        const auto hairMaterial =
            loadMaterial("res/Materials/Taffy/Hair.material.json");

        sceneMaterials.push_back(bodyMaterial);
        sceneMaterials.push_back(faceMaterial);
        sceneMaterials.push_back(hairMaterial);
        for (size_t submeshIndex = 0;
             submeshIndex < taffyMesh->getSubmeshes().size();
             ++submeshIndex) {
            const auto& materialName =
                taffyMesh->getSubmeshes()[submeshIndex].getMaterialName();
            if (materialName.find("Face") != std::string::npos) {
                taffyMesh->setSubmeshMaterial(submeshIndex, faceMaterial);
            } else if (materialName.find("Hair") != std::string::npos) {
                taffyMesh->setSubmeshMaterial(submeshIndex, hairMaterial);
            } else {
                taffyMesh->setSubmeshMaterial(submeshIndex, bodyMaterial);
            }
        }

        taffy->setMesh(taffyMesh);
        taffy->setMaterial(bodyMaterial);
        placeModelOnFloor(taffy, taffyMesh, 3.7f, 0.15f);
        sceneMeshes.push_back(taffyMesh);
        scene->addObject(taffy);
    } else {
        LOG_ERROR("Failed to load the Cornell Box Taffy model");
    }

    // The renderer supports all three common light categories. The area light
    // is the principal Cornell-box emitter; point and directional lights are
    // deliberately subtle fills and can be tuned live in Scene Inspector.
    scene->addLight(AreaLight::create(
        TSVec3f(0.0f, 4.85f, -0.25f),
        TSVec3f(0.0f, -1.0f, 0.0f),
        TSVec3f(1.0f, 0.95f, 0.86f),
        34.0f,
        1.5f,
        1.0f,
        false,
        "CeilingAreaLight"));
    scene->addLight(PointLight::create(
        TSVec3f(0.0f, 2.2f, 1.8f),
        TSVec3f(1.0f, 0.78f, 0.58f),
        0.8f,
        1.0f,
        0.22f,
        0.20f,
        "WarmFillPoint"));
    scene->addLight(DirectionalLight::create(
        TSVec3f(-0.35f, -1.0f, -0.25f),
        TSVec3f(0.62f, 0.72f, 1.0f),
        0.08f,
        "CoolFillDirectional"));

    return scene;
}

} // namespace

int main()
{
    Tasrovy::Log::Logger::Init();
    setWorkingDirectoryToProjectRoot();

    Tasrovy::Windowing::Window window(1280, 800, "TasrovyRenderer - Cornell Box");
    const float cameraAspect =
        static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight());

    // Scene objects intentionally keep weak references to render resources.
    // These containers own the code-defined resources for the application lifetime.
    std::vector<std::shared_ptr<Material>> sceneMaterials;
    std::vector<std::shared_ptr<Mesh>> sceneMeshes;
    auto scene = createCornellBoxScene(
        cameraAspect, sceneMaterials, sceneMeshes);
    auto pipeline = DeferredPipeline::create();
    pipeline->GenPass(scene);

    Tasrovy::Renderer::SceneRenderer renderer(window, 4);
    renderer.setScene(scene);
    renderer.setPipeline(pipeline);
    renderer.start();

    LOG_INFO("Cornell Box submitted to the deferred renderer");
    while (!window.shouldClose()) {
        window.pollEvents();
    }

    renderer.stop();
    LOG_INFO("Application exiting");
    return 0;
}
