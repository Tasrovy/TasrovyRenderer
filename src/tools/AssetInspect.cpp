#include "AssetLoader.hpp"
#include "Logger.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const char* textureTypeName(aiTextureType type) {
    switch (type) {
    case aiTextureType_DIFFUSE: return "Diffuse";
    case aiTextureType_SPECULAR: return "Specular";
    case aiTextureType_AMBIENT: return "Ambient";
    case aiTextureType_EMISSIVE: return "Emissive";
    case aiTextureType_HEIGHT: return "Height";
    case aiTextureType_NORMALS: return "Normals";
    case aiTextureType_SHININESS: return "Shininess";
    case aiTextureType_OPACITY: return "Opacity";
    case aiTextureType_DISPLACEMENT: return "Displacement";
    case aiTextureType_LIGHTMAP: return "Lightmap";
    case aiTextureType_REFLECTION: return "Reflection";
    case aiTextureType_BASE_COLOR: return "BaseColor";
    case aiTextureType_NORMAL_CAMERA: return "NormalCamera";
    case aiTextureType_EMISSION_COLOR: return "EmissionColor";
    case aiTextureType_METALNESS: return "Metalness";
    case aiTextureType_DIFFUSE_ROUGHNESS: return "DiffuseRoughness";
    case aiTextureType_AMBIENT_OCCLUSION: return "AmbientOcclusion";
    case aiTextureType_SHEEN: return "Sheen";
    case aiTextureType_CLEARCOAT: return "Clearcoat";
    case aiTextureType_TRANSMISSION: return "Transmission";
    default: return "Unknown";
    }
}

void printMaterialTextures(const aiMaterial& material, aiTextureType type) {
    const unsigned int count = material.GetTextureCount(type);
    for (unsigned int i = 0; i < count; ++i) {
        aiString path;
        aiTextureMapping mapping = aiTextureMapping_UV;
        unsigned int uvIndex = 0;
        float blend = 1.0f;
        aiTextureOp op = aiTextureOp_Multiply;
        aiTextureMapMode mapMode[3] = {
            aiTextureMapMode_Wrap,
            aiTextureMapMode_Wrap,
            aiTextureMapMode_Wrap
        };

        if (material.GetTexture(type, i, &path, &mapping, &uvIndex, &blend, &op, mapMode) == AI_SUCCESS) {
            std::cout
                << "    texture " << textureTypeName(type)
                << "[" << i << "] path='" << path.C_Str()
                << "' uv=" << uvIndex
                << " mapping=" << static_cast<int>(mapping)
                << " blend=" << blend
                << "\n";
        }
    }
}

void printSceneDetails(const std::string& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_FindInstances);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cout << "Assimp inspect failed: " << importer.GetErrorString() << "\n";
        return;
    }

    std::cout
        << "Assimp scene: meshes=" << scene->mNumMeshes
        << " materials=" << scene->mNumMaterials
        << " animations=" << scene->mNumAnimations
        << " embeddedTextures=" << scene->mNumTextures
        << "\n";

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        std::cout
            << "  mesh[" << i << "] name='" << mesh->mName.C_Str()
            << "' vertices=" << mesh->mNumVertices
            << " faces=" << mesh->mNumFaces
            << " materialIndex=" << mesh->mMaterialIndex
            << " bones=" << mesh->mNumBones
            << " uvChannels=" << mesh->GetNumUVChannels()
            << " colorChannels=" << mesh->GetNumColorChannels()
            << "\n";

        for (unsigned int uv = 0; uv < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++uv) {
            if (!mesh->HasTextureCoords(uv)) {
                continue;
            }
            std::cout
                << "    uv" << uv
                << " components=" << mesh->mNumUVComponents[uv]
                << "\n";
        }
    }

    const std::vector<aiTextureType> textureTypes = {
        aiTextureType_BASE_COLOR,
        aiTextureType_DIFFUSE,
        aiTextureType_NORMALS,
        aiTextureType_NORMAL_CAMERA,
        aiTextureType_METALNESS,
        aiTextureType_DIFFUSE_ROUGHNESS,
        aiTextureType_AMBIENT_OCCLUSION,
        aiTextureType_EMISSIVE,
        aiTextureType_EMISSION_COLOR,
        aiTextureType_SPECULAR,
        aiTextureType_SHININESS,
        aiTextureType_OPACITY
    };

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* material = scene->mMaterials[i];
        aiString name;
        material->Get(AI_MATKEY_NAME, name);
        std::cout << "  material[" << i << "] name='" << name.C_Str() << "'\n";
        for (const auto type : textureTypes) {
            printMaterialTextures(*material, type);
        }
    }
}

void compareModelUvs(const Tasrovy::FS::Model& lhs, const Tasrovy::FS::Model& rhs) {
    std::unordered_map<std::string, const Tasrovy::FS::Submesh*> rhsSubmeshes;
    for (const auto& submesh : rhs.GetSubmeshes()) {
        rhsSubmeshes[submesh.materialName] = &submesh;
    }

    std::cout << "UV comparison by material and face corner:\n";
    for (const auto& lhsSubmesh : lhs.GetSubmeshes()) {
        const auto found = rhsSubmeshes.find(lhsSubmesh.materialName);
        if (found == rhsSubmeshes.end()) {
            std::cout << "  material='" << lhsSubmesh.materialName << "' missing in second model\n";
            continue;
        }

        const auto& rhsSubmesh = *found->second;
        const uint32_t count = std::min(lhsSubmesh.indexCount, rhsSubmesh.indexCount);
        uint32_t directMismatchCount = 0;
        uint32_t flipYMismatchCount = 0;
        float maxDirectError = 0.0f;
        float maxFlipYError = 0.0f;

        for (uint32_t i = 0; i < count; ++i) {
            const auto lhsIndex = lhs.GetIndices()[lhsSubmesh.indexOffset + i];
            const auto rhsIndex = rhs.GetIndices()[rhsSubmesh.indexOffset + i];
            const auto lhsUv = lhs.GetVertices()[lhsIndex].uv0;
            const auto rhsUv = rhs.GetVertices()[rhsIndex].uv0;
            const float directError = std::max(
                std::abs(lhsUv.x - rhsUv.x),
                std::abs(lhsUv.y - rhsUv.y));
            const float flipYError = std::max(
                std::abs(lhsUv.x - rhsUv.x),
                std::abs((1.0f - lhsUv.y) - rhsUv.y));
            maxDirectError = std::max(maxDirectError, directError);
            maxFlipYError = std::max(maxFlipYError, flipYError);
            directMismatchCount += directError > 1e-5f ? 1u : 0u;
            flipYMismatchCount += flipYError > 1e-5f ? 1u : 0u;
        }

        std::cout
            << "  material='" << lhsSubmesh.materialName
            << "' corners=" << count
            << " directMismatch=" << directMismatchCount
            << " maxDirectError=" << maxDirectError
            << " flipYMismatch=" << flipYMismatchCount
            << " maxFlipYError=" << maxFlipYError
            << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    Tasrovy::Log::Logger::Init();

    const std::string path = argc > 1 ? argv[1] : "res/Models/Taffy/Taffy.obj";
    if (!std::filesystem::exists(path)) {
        std::cerr << "Asset does not exist: " << path << "\n";
        return 1;
    }

    Tasrovy::FS::AssetLoader loader;
    const auto model = loader.LoadModel(path);
    if (!model) {
        std::cerr << "Tasrovy filesystem failed to load model: " << path << "\n";
        return 2;
    }

    std::cout
        << "Tasrovy filesystem model: vertices=" << model->GetVertices().size()
        << " indices=" << model->GetIndices().size()
        << " submeshes=" << model->GetSubmeshes().size()
        << " bones=" << model->GetBones().size()
        << "\n";

    for (size_t i = 0; i < model->GetSubmeshes().size(); ++i) {
        const auto& submesh = model->GetSubmeshes()[i];
        std::cout
            << "  submesh[" << i << "] material='" << submesh.materialName
            << "' indexOffset=" << submesh.indexOffset
            << " indexCount=" << submesh.indexCount
            << "\n";
    }

    if (argc > 2) {
        const std::string comparisonPath = argv[2];
        const auto comparisonModel = loader.LoadModel(comparisonPath);
        if (!comparisonModel) {
            std::cerr << "Failed to load comparison model: " << comparisonPath << "\n";
            return 3;
        }
        compareModelUvs(*model, *comparisonModel);
    }

    printSceneDetails(path);
    Tasrovy::Log::Logger::Shutdown();
    return 0;
}
