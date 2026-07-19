#pragma once

#include <filesystem>
#include <memory>
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

class SceneSerializer {
public:
    static bool save(
        const std::filesystem::path& path,
        const std::shared_ptr<Render::Scene>& scene);
    static bool load(
        const std::filesystem::path& path,
        float cameraAspect,
        SceneArchive& archive);
};

} // namespace Tasrovy::Core
