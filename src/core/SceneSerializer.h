#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Tasrovy::Render {
class Material;
class Mesh;
class Scene;
class Texture;
}

namespace Tasrovy::Core {

struct SceneArchive {
    std::shared_ptr<Render::Scene> scene;
    std::vector<std::shared_ptr<Render::Material>> materials;
    std::vector<std::shared_ptr<Render::Mesh>> meshes;
    std::vector<std::shared_ptr<Render::Texture>> textures;
};

struct SceneMetadata {
    std::filesystem::path path;
    std::string name;
    uint32_t version = 0;
};

class SceneSerializer {
public:
    static bool inspect(
        const std::filesystem::path& path,
        SceneMetadata& metadata);
    static bool save(
        const std::filesystem::path& path,
        const std::shared_ptr<Render::Scene>& scene);
    static bool load(
        const std::filesystem::path& path,
        float cameraAspect,
        SceneArchive& archive);
};

} // namespace Tasrovy::Core
