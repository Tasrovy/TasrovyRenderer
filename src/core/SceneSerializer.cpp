#include "SceneSerializer.h"

#include "AssetLoader.hpp"
#include "Camera.h"
#include "Light.h"
#include "Logger.hpp"
#include "Material.h"
#include "Mesh.h"
#include "Object.h"
#include "Primitive.h"
#include "Scene.h"
#include "Skybox.h"
#include "Texture.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace Tasrovy::Core {

using nlohmann::json;
using namespace Tasrovy::Render;

namespace {

json vec3ToJson(const TSVec3f& value) {
    return json::array({value.x, value.y, value.z});
}

json vec4ToJson(const TSVec4f& value) {
    return json::array({value.x, value.y, value.z, value.w});
}

TSVec3f jsonToVec3(const json& value, const TSVec3f& fallback = TSVec3f(0.0f)) {
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return TSVec3f(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

TSVec4f jsonToVec4(const json& value, const TSVec4f& fallback = TSVec4f(0.0f)) {
    if (!value.is_array() || value.size() < 4) {
        return fallback;
    }
    return TSVec4f(
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>());
}

json serializeMaterial(const std::shared_ptr<Material>& material) {
    if (!material) {
        return nullptr;
    }

    json data;
    data["surface"] = static_cast<int>(material->getSurface());
    data["alphaCutoff"] = material->getAlphaCutoff();
    data["castShadows"] = material->castsShadows();
    for (const auto& [name, value] : material->getFloatParams()) {
        data["floats"][name] = value;
    }
    for (const auto& [name, value] : material->getVec3Params()) {
        data["vec3"][name] = vec3ToJson(value);
    }
    for (const auto& [name, value] : material->getVec4Params()) {
        data["vec4"][name] = vec4ToJson(value);
    }
    for (const auto& [semantic, binding] : material->getSemanticTextureBindings()) {
        data["semanticTextures"].push_back({
            {"semantic", static_cast<int>(semantic)},
            {"binding", binding.binding},
            {"path", binding.path}
        });
    }
    for (const auto& [slot, binding] : material->getTextureBindings()) {
        data["namedTextures"].push_back({
            {"slot", slot}, {"binding", binding.binding}, {"path", binding.path}
        });
    }
    return data;
}

std::shared_ptr<Material> deserializeMaterial(const json& data, SceneArchive& archive) {
    if (data.is_null() || !data.is_object()) {
        return nullptr;
    }
    auto material = Material::create();
    material->setSurface(static_cast<MaterialSurface>(data.value("surface", 0)));
    material->setAlphaCutoff(data.value("alphaCutoff", 0.5f));
    material->setCastShadows(data.value("castShadows", true));
    for (const auto& [name, value] : data.value("floats", json::object()).items()) {
        material->setFloat(name, value.get<float>());
    }
    for (const auto& [name, value] : data.value("vec3", json::object()).items()) {
        material->setVec3(name, jsonToVec3(value));
    }
    for (const auto& [name, value] : data.value("vec4", json::object()).items()) {
        material->setVec4(name, jsonToVec4(value));
    }
    for (const auto& texture : data.value("semanticTextures", json::array())) {
        material->setTexture(
            static_cast<MaterialTextureSemantic>(texture.value("semantic", 0)),
            texture.value("binding", 0u),
            texture.value("path", std::string()));
    }
    for (const auto& texture : data.value("namedTextures", json::array())) {
        material->setTexture(
            texture.value("slot", std::string()),
            texture.value("binding", 0u),
            texture.value("path", std::string()));
    }
    archive.materials.push_back(material);
    return material;
}

json serializeTransform(const Object& object) {
    const TSQuatf rotation = object.getRotationQuat();
    return {
        {"position", vec3ToJson(object.getPosition())},
        {"rotation", json::array({rotation.x, rotation.y, rotation.z, rotation.w})},
        {"scale", vec3ToJson(object.getScale())}
    };
}

void deserializeTransform(const json& data, Object& object) {
    object.setPosition(jsonToVec3(data.value("position", json::array())));
    object.setScale(jsonToVec3(data.value("scale", json::array()), TSVec3f(1.0f)));
    const auto rotation = data.value("rotation", json::array());
    if (rotation.is_array() && rotation.size() >= 4) {
        object.setRotation(TSQuatf(
            rotation[3].get<float>(), rotation[0].get<float>(),
            rotation[1].get<float>(), rotation[2].get<float>()));
    }
}

json serializeObject(const std::shared_ptr<Object>& object) {
    json data;
    data["name"] = object->getName();
    data["active"] = object->isActive();
    data["flipProjectionY"] = object->getFlipProjectionY();
    data["transform"] = serializeTransform(*object);

    if (const auto skybox = std::dynamic_pointer_cast<Skybox>(object)) {
        data["type"] = "skybox";
        if (const auto cubemap = skybox->getCubemap()) {
            data["cubemap"] = cubemap->getFilePath();
        }
    } else if (std::dynamic_pointer_cast<Plane>(object)) {
        data["type"] = "plane";
    } else if (std::dynamic_pointer_cast<Cube>(object)) {
        data["type"] = "cube";
    } else if (const auto sphere = std::dynamic_pointer_cast<Sphere>(object)) {
        data["type"] = "sphere";
        data["sectors"] = sphere->getSectors();
        data["stacks"] = sphere->getStacks();
    } else if (const auto mesh = object->getMesh(); mesh && !mesh->getSourcePath().empty()) {
        data["type"] = "model";
        data["model"] = mesh->getSourcePath();
    } else {
        data["type"] = "object";
    }

    data["material"] = serializeMaterial(object->getMaterial());
    if (const auto mesh = object->getMesh()) {
        for (size_t index = 0; index < mesh->getSubmeshes().size(); ++index) {
            data["submeshMaterials"].push_back({
                {"index", index},
                {"name", mesh->getSubmeshes()[index].getMaterialName()},
                {"material", serializeMaterial(object->getSubmeshMaterial(index))}
            });
        }
    }
    for (const auto& child : object->getChildren()) {
        if (child) {
            data["children"].push_back(serializeObject(child));
        }
    }
    return data;
}

std::shared_ptr<Object> deserializeObject(
    const json& data,
    SceneArchive& archive,
    Tasrovy::FS::AssetLoader& loader) {
    const std::string type = data.value("type", "object");
    const std::string name = data.value("name", type);
    std::shared_ptr<Object> object;

    if (type == "plane") {
        object = Plane::create(name);
    } else if (type == "cube") {
        object = Cube::create(name);
    } else if (type == "sphere") {
        object = Sphere::create(name, data.value("sectors", 32u), data.value("stacks", 16u));
    } else if (type == "skybox") {
        auto skybox = Skybox::create(name);
        const std::string cubemapPath = data.value("cubemap", std::string());
        if (!cubemapPath.empty()) {
            auto texture = Texture::createCubemap(cubemapPath);
            archive.textures.push_back(texture);
            skybox->setCubemap(texture);
        }
        object = skybox;
    } else if (type == "model") {
        const std::string modelPath = data.value("model", std::string());
        const auto model = loader.LoadModel(modelPath);
        if (!model) {
            LOG_ERROR("SceneSerializer: failed to load model '{}'", modelPath);
            return nullptr;
        }
        auto mesh = Mesh::fromModel(*model);
        mesh->setSourcePath(modelPath);
        archive.meshes.push_back(mesh);
        object = Object::create(name);
        object->setMesh(mesh);
    } else {
        object = Object::create(name);
    }

    object->setActive(data.value("active", true));
    object->setFlipProjectionY(data.value("flipProjectionY", true));
    deserializeTransform(data.value("transform", json::object()), *object);
    object->setMaterial(deserializeMaterial(
        data.contains("material") ? data["material"] : json(nullptr),
        archive));

    if (const auto mesh = object->getMesh()) {
        for (const auto& entry : data.value("submeshMaterials", json::array())) {
            const size_t index = entry.value("index", static_cast<size_t>(0));
            mesh->setSubmeshMaterial(
                index,
                deserializeMaterial(
                    entry.contains("material") ? entry["material"] : json(nullptr),
                    archive));
        }
    }
    for (const auto& childData : data.value("children", json::array())) {
        if (auto child = deserializeObject(childData, archive, loader)) {
            object->addChild(child);
        }
    }
    return object;
}

json serializeLight(const Light& light) {
    json data = {
        {"name", light.getName()},
        {"direction", vec3ToJson(light.getDirection())},
        {"color", vec3ToJson(light.getColor())},
        {"intensity", light.getIntensity()}
    };
    if (const auto* area = dynamic_cast<const AreaLight*>(&light)) {
        data["type"] = "area";
        data["position"] = vec3ToJson(area->getPosition());
        data["width"] = area->getWidth();
        data["height"] = area->getHeight();
        data["twoSided"] = area->isTwoSided();
    } else if (const auto* point = dynamic_cast<const PointLight*>(&light)) {
        data["type"] = "point";
        data["position"] = vec3ToJson(point->getPosition());
        data["constant"] = point->getConstant();
        data["linear"] = point->getLinear();
        data["quadratic"] = point->getQuadratic();
    } else if (const auto* spot = dynamic_cast<const SpotLight*>(&light)) {
        data["type"] = "spot";
        data["position"] = vec3ToJson(spot->getPosition());
        data["cutoff"] = spot->getCutoff();
    } else {
        data["type"] = "directional";
    }
    return data;
}

std::unique_ptr<Light> deserializeLight(const json& data) {
    const auto type = data.value("type", "directional");
    const auto name = data.value("name", std::string());
    const auto direction = jsonToVec3(data.value("direction", json::array()), TSVec3f(0.0f, -1.0f, 0.0f));
    const auto color = jsonToVec3(data.value("color", json::array()), TSVec3f(1.0f));
    const float intensity = data.value("intensity", 1.0f);
    if (type == "area") {
        return AreaLight::create(
            jsonToVec3(data.value("position", json::array())), direction, color, intensity,
            data.value("width", 1.0f), data.value("height", 1.0f),
            data.value("twoSided", false), name);
    }
    if (type == "point") {
        return PointLight::create(
            jsonToVec3(data.value("position", json::array())), color, intensity,
            data.value("constant", 1.0f), data.value("linear", 0.09f),
            data.value("quadratic", 0.032f), name);
    }
    if (type == "spot") {
        return SpotLight::create(
            jsonToVec3(data.value("position", json::array())), direction, color, intensity,
            data.value("cutoff", 12.5f), name);
    }
    return DirectionalLight::create(direction, color, intensity, name);
}

} // namespace

bool SceneSerializer::save(
    const std::filesystem::path& path,
    const std::shared_ptr<Scene>& scene) {
    if (!scene) {
        return false;
    }
    try {
        json root;
        root["format"] = "TasrovyScene";
        root["version"] = 1;
        root["name"] = scene->getName();
        root["primaryCamera"] = scene->getPrimaryCamera()
            ? scene->getPrimaryCamera()->getName()
            : std::string();
        for (const auto& camera : scene->getCameras()) {
            const TSQuatf rotation = camera->getRotationQuat();
            root["cameras"].push_back({
                {"name", camera->getName()},
                {"position", vec3ToJson(camera->getPosition())},
                {"rotation", json::array({rotation.x, rotation.y, rotation.z, rotation.w})},
                {"fov", camera->getFOV()},
                {"aspect", camera->getAspect()},
                {"near", camera->getNearPlane()},
                {"far", camera->getFarPlane()}
            });
        }
        for (const auto& light : scene->getLights()) {
            if (light) root["lights"].push_back(serializeLight(*light));
        }
        for (const auto& object : scene->getObjects()) {
            if (object) root["objects"].push_back(serializeObject(object));
        }

        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::trunc);
        if (!output) {
            LOG_ERROR("SceneSerializer: cannot open '{}' for writing", path.string());
            return false;
        }
        output << root.dump(2) << '\n';
        LOG_INFO("SceneSerializer: saved scene '{}'", path.string());
        return output.good();
    } catch (const std::exception& error) {
        LOG_ERROR("SceneSerializer: save failed '{}': {}", path.string(), error.what());
        return false;
    }
}

bool SceneSerializer::load(
    const std::filesystem::path& path,
    float cameraAspect,
    SceneArchive& archive) {
    try {
        std::ifstream input(path);
        if (!input) return false;
        json root;
        input >> root;
        if (root.value("format", std::string()) != "TasrovyScene" ||
            root.value("version", 0) != 1) {
            LOG_ERROR("SceneSerializer: unsupported scene format '{}'", path.string());
            return false;
        }

        SceneArchive loaded;
        loaded.scene = Scene::create(root.value("name", "Scene"));
        const std::string primaryCamera = root.value("primaryCamera", std::string());
        for (const auto& cameraData : root.value("cameras", json::array())) {
            auto camera = Camera::create(
                jsonToVec3(cameraData.value("position", json::array())),
                TSVec3f(0.0f),
                cameraData.value("fov", 45.0f),
                cameraAspect,
                cameraData.value("near", 0.1f),
                cameraData.value("far", 100.0f),
                cameraData.value("name", std::string()));
            const auto rotation = cameraData.value("rotation", json::array());
            if (rotation.is_array() && rotation.size() >= 4) {
                camera->setRotation(TSQuatf(
                    rotation[3].get<float>(), rotation[0].get<float>(),
                    rotation[1].get<float>(), rotation[2].get<float>()));
            }
            Camera* cameraPtr = camera.get();
            loaded.scene->addCamera(std::move(camera));
            if (cameraPtr->getName() == primaryCamera) {
                loaded.scene->setPrimaryCamera(cameraPtr);
            }
        }
        if (!loaded.scene->getPrimaryCamera() && !loaded.scene->getCameras().empty()) {
            loaded.scene->setPrimaryCamera(loaded.scene->getCameras().front().get());
        }

        for (const auto& lightData : root.value("lights", json::array())) {
            loaded.scene->addLight(deserializeLight(lightData));
        }
        Tasrovy::FS::AssetLoader loader;
        for (const auto& objectData : root.value("objects", json::array())) {
            if (auto object = deserializeObject(objectData, loaded, loader)) {
                loaded.scene->addObject(object);
            }
        }
        archive = std::move(loaded);
        LOG_INFO("SceneSerializer: loaded scene '{}'", path.string());
        return true;
    } catch (const std::exception& error) {
        LOG_ERROR("SceneSerializer: load failed '{}': {}", path.string(), error.what());
        return false;
    }
}

} // namespace Tasrovy::Core
