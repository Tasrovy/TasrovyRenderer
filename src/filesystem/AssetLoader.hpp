#pragma once
#include <assimp/Importer.hpp>
#include <assimp/mesh.h>
#include <memory>
#include <string>
#include "Model.hpp"
#include "Image.hpp"
#include "Anim.hpp"

namespace Tasrovy::FS {

class AssetLoader {
public:
    AssetLoader() = default;
    ~AssetLoader() = default;

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    std::shared_ptr<Model> LoadModel(const std::string& path);
    Image LoadImage(const std::string& path);
    Anim LoadAnim(const std::string& path);

private:
    void ProcessNode(aiNode* node, const aiScene* scene, Model& model);
    void ProcessMesh(aiMesh* mesh, const aiScene* scene, Model& model);
    void ProcessBones(aiMesh* mesh, Model& model);
};

}
