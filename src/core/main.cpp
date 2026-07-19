#include <volk.h>

#include <Logger.hpp>
#include <SceneRenderer.h>
#include <Window.h>

#include "Camera.h"
#include "DeferredPipeline.h"
#include "AssetLoader.hpp"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "Object.h"
#include "Primitive.h"
#include "Scene.h"
#include "SceneSerializer.h"

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

std::shared_ptr<Material> createSolidMaterial(
    const TSVec3f& linearColor,
    bool castsShadows = true)
{
    auto material = Material::create();
    material->setVec4(
        "baseColorFactor",
        TSVec4f(linearColor.x, linearColor.y, linearColor.z, 1.0f));
    material->setFloat("metallic", 0.0f);
    material->setFloat("roughness", 0.85f);
    material->setFloat("ao", 1.0f);
    material->setFloat("rimStrength", 0.0f);
    material->setFloat("rimPower", 3.0f);
    material->setVec3("rimColor", TSVec3f(1.0f));
    material->setCastShadows(castsShadows);
    return material;
}

std::shared_ptr<Material> createTaffyMaterial(const std::string& baseColorPath)
{
    auto material = Material::create();
    material->setVec4("baseColorFactor", TSVec4f(1.0f));
    material->setFloat("metallic", 0.0f);
    material->setFloat("roughness", 0.75f);
    material->setFloat("ao", 1.0f);
    material->setFloat("rimStrength", 0.22f);
    material->setFloat("rimPower", 3.5f);
    material->setVec3("rimColor", TSVec3f(1.0f, 0.72f, 0.78f));
    material->setTexture(MaterialTextureSemantic::BaseColor, 1, baseColorPath);
    return material;
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
    const auto floorMaterial = createSolidMaterial(TSVec3f(0.73f, 0.73f, 0.73f));
    const auto ceilingMaterial = createSolidMaterial(TSVec3f(0.73f, 0.73f, 0.73f));
    const auto backWallMaterial = createSolidMaterial(TSVec3f(0.73f, 0.73f, 0.73f));
    const auto leftWallMaterial = createSolidMaterial(TSVec3f(0.63f, 0.065f, 0.05f));
    const auto rightWallMaterial = createSolidMaterial(TSVec3f(0.14f, 0.45f, 0.091f));
    const auto lightPanel = createSolidMaterial(TSVec3f(1.0f), false);
    lightPanel->setFloat("emissiveIntensity", 12.0f);
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
        auto taffyMesh = Mesh::fromModel(*taffyModel);
        taffyMesh->setSourcePath("res/Models/Taffy/Taffy.obj");
        auto taffy = Object::create("Taffy");
        const auto bodyMaterial = createTaffyMaterial("res/Textures/Taffy/cloth.png");
        const auto faceMaterial = createTaffyMaterial("res/Textures/Taffy/face.png");
        const auto hairMaterial = createTaffyMaterial("res/Textures/Taffy/hair.png");

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
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("Failed to initialize volk!");
        return -1;
    }

    Tasrovy::Log::Logger::Init();
    setWorkingDirectoryToProjectRoot();

    Tasrovy::Windowing::Window window(1280, 800, "TasrovyRenderer - Cornell Box");
    constexpr const char* scenePath = "res/Scenes/CornellTaffy.scene.json";
    const float cameraAspect =
        static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight());

    // Scene objects intentionally use weak references to render resources.
    // The archive owns every resource restored from disk for the scene lifetime.
    Tasrovy::Core::SceneArchive sceneArchive;
    if (!Tasrovy::Core::SceneSerializer::load(scenePath, cameraAspect, sceneArchive)) {
        LOG_INFO("No usable saved scene found; creating the default Cornell scene");
        sceneArchive.scene = createCornellBoxScene(
            cameraAspect,
            sceneArchive.materials,
            sceneArchive.meshes);
        Tasrovy::Core::SceneSerializer::save(scenePath, sceneArchive.scene);
    }
    auto scene = sceneArchive.scene;
    auto pipeline = DeferredPipeline::create();
    pipeline->GenPass(scene);

    Tasrovy::RHI::SceneRenderer renderer(window, 4);
    renderer.setScene(scene);
    renderer.setPipeline(pipeline);
    renderer.start();

    LOG_INFO("Cornell Box submitted to the deferred renderer");
    while (!window.shouldClose()) {
        window.pollEvents();
    }

    renderer.stop();
    Tasrovy::Core::SceneSerializer::save(scenePath, scene);
    LOG_INFO("Application exiting");
    return 0;
}
