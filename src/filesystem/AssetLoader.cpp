#include "AssetLoader.hpp"
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <Logger.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Tasrovy {

std::shared_ptr<Model> AssetLoader::LoadModel(const std::string& path) {
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_FindInstances
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        LOG_ERROR("Assimp failed to load model '{}': {}", path, importer.GetErrorString());
        return nullptr;
    }

    auto model = std::make_unique<Model>();
    ProcessNode(scene->mRootNode, scene, *model);

    LOG_INFO("Loaded model '{}': {} vertices, {} indices, {} submeshes, {} bones",
        path, model->GetVertices().size(), model->GetIndices().size(),
        model->GetSubmeshes().size(), model->GetBones().size());

    return model;
}

void AssetLoader::ProcessNode(aiNode* node, const aiScene* scene, Model& model) {
    for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        ProcessMesh(mesh, scene, model);
    }

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene, model);
    }
}

void AssetLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, Model& model) {
    Submesh submesh;
    submesh.indexOffset = static_cast<uint32_t>(model.GetIndices().size());

    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    aiString matName;
    material->Get(AI_MATKEY_NAME, matName);
    submesh.materialName = matName.C_Str();

    for (uint32_t i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex{};

        // Position
        vertex.position = {
            mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z
        };

        // Normal
        if (mesh->HasNormals()) {
            vertex.normal = {
                mesh->mNormals[i].x,
                mesh->mNormals[i].y,
                mesh->mNormals[i].z
            };
        }

        // Tangent
        if (mesh->HasTangentsAndBitangents()) {
            vertex.tangent = {
                mesh->mTangents[i].x,
                mesh->mTangents[i].y,
                mesh->mTangents[i].z
            };
        }

        // Vertex color
        if (mesh->HasVertexColors(0)) {
            vertex.vertexColor = {
                mesh->mColors[0][i].r,
                mesh->mColors[0][i].g,
                mesh->mColors[0][i].b
            };
        }

        // UV sets
        auto setUv = [&](glm::vec2& uv, uint32_t set) {
            if (mesh->HasTextureCoords(set)) {
                uv = { mesh->mTextureCoords[set][i].x, mesh->mTextureCoords[set][i].y };
            }
        };
        setUv(vertex.uv0, 0);
        setUv(vertex.uv1, 1);
        setUv(vertex.uv2, 2);
        setUv(vertex.uv3, 3);

        model.GetVertices().push_back(vertex);
    }

    // Indices
    for (uint32_t i = 0; i < mesh->mNumFaces; ++i) {
        aiFace& face = mesh->mFaces[i];
        for (uint32_t j = 0; j < face.mNumIndices; ++j) {
            model.GetIndices().push_back(face.mIndices[j]);
        }
    }

    submesh.indexCount = static_cast<uint32_t>(model.GetIndices().size()) - submesh.indexOffset;
    model.GetSubmeshes().push_back(submesh);

    // Bones
    ProcessBones(mesh, model);
}

void AssetLoader::ProcessBones(aiMesh* mesh, Model& model) {
    auto& bones = model.GetBones();
    auto& vertices = model.GetVertices();

    for (uint32_t i = 0; i < mesh->mNumBones; ++i) {
        aiBone* bone = mesh->mBones[i];

        // Check if bone already exists
        auto it = std::find_if(bones.begin(), bones.end(),
            [&](const Bone& b) { return b.name == bone->mName.C_Str(); });

        if (it == bones.end()) {
            Bone newBone;
            newBone.name = bone->mName.C_Str();
            glm::mat4 offset;
            memcpy(&offset, &bone->mOffsetMatrix, sizeof(glm::mat4));
            newBone.offsetMatrix = glm::transpose(offset);
            bones.push_back(newBone);
        }
    }
}

Image AssetLoader::LoadImage(const std::string& path) {
    Image image;
    if (!image.LoadFromFile(path)) {
        LOG_ERROR("Failed to load image '{}'", path);
    }
    return image;
}

Anim AssetLoader::LoadAnim(const std::string& path) {
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals
    );

    if (!scene || !scene->mRootNode) {
        LOG_ERROR("Assimp failed to load animation '{}': {}", path, importer.GetErrorString());
        return Anim();
    }

    Anim anim;
    anim.SetName(path);

    if (scene->mNumAnimations == 0) {
        LOG_WARN("No animations found in '{}'", path);
        return anim;
    }

    // Process the first animation track
    aiAnimation* aiAnim = scene->mAnimations[0];
    anim.SetDuration(aiAnim->mDuration);
    anim.SetTicksPerSecond(aiAnim->mTicksPerSecond > 0 ? aiAnim->mTicksPerSecond : 25.0);

    for (uint32_t i = 0; i < aiAnim->mNumChannels; ++i) {
        aiNodeAnim* aiChannel = aiAnim->mChannels[i];
        BoneChannel channel;
        channel.boneName = aiChannel->mNodeName.C_Str();

        // Position keys
        for (uint32_t j = 0; j < aiChannel->mNumPositionKeys; ++j) {
            auto& key = aiChannel->mPositionKeys[j];
            channel.positionKeys.push_back({
                key.mTime,
                {key.mValue.x, key.mValue.y, key.mValue.z}
            });
        }

        // Rotation keys
        for (uint32_t j = 0; j < aiChannel->mNumRotationKeys; ++j) {
            auto& key = aiChannel->mRotationKeys[j];
            channel.rotationKeys.push_back({
                key.mTime,
                {key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z}
            });
        }

        // Scaling keys
        for (uint32_t j = 0; j < aiChannel->mNumScalingKeys; ++j) {
            auto& key = aiChannel->mScalingKeys[j];
            channel.scalingKeys.push_back({
                key.mTime,
                {key.mValue.x, key.mValue.y, key.mValue.z}
            });
        }

        anim.AddChannel(std::move(channel));
    }

    LOG_INFO("Loaded animation '{}': {} channels, {:.2f}s duration, {:.1f} TPS",
        path, anim.GetChannels().size(), anim.GetDurationInSeconds(), anim.GetTicksPerSecond());

    return anim;
}

}
