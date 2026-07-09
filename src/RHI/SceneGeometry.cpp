#include "SceneGeometry.h"

#include <tiny_obj_loader.h>

#include <Logger.hpp>
#include <stdexcept>
#include <unordered_map>

namespace std {
size_t hash<Tasrovy::RHI::SceneVertex>::operator()(Tasrovy::RHI::SceneVertex const& vertex) const noexcept {
    size_t seed = 0;
    auto hashCombine = [&seed](size_t v) {
        seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    hashCombine(std::hash<float>()(vertex.position.x));
    hashCombine(std::hash<float>()(vertex.position.y));
    hashCombine(std::hash<float>()(vertex.position.z));
    hashCombine(std::hash<float>()(vertex.normal.x));
    hashCombine(std::hash<float>()(vertex.normal.y));
    hashCombine(std::hash<float>()(vertex.normal.z));
    hashCombine(std::hash<float>()(vertex.texCoord.x));
    hashCombine(std::hash<float>()(vertex.texCoord.y));
    return seed;
}
} // namespace std

namespace Tasrovy::RHI {

namespace {
void calculateTangentsAndBitangents(SceneModelData& model) {
    for (size_t i = 0; i < model.indices.size(); i += 3) {
        SceneVertex& v0 = model.vertices[model.indices[i + 0]];
        SceneVertex& v1 = model.vertices[model.indices[i + 1]];
        SceneVertex& v2 = model.vertices[model.indices[i + 2]];

        TSVec3f edge1 = v1.position - v0.position;
        TSVec3f edge2 = v2.position - v0.position;
        TSVec2f deltaUV1 = v1.texCoord - v0.texCoord;
        TSVec2f deltaUV2 = v2.texCoord - v0.texCoord;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        TSVec3f tangent;
        TSVec3f bitangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        v0.tangent += tangent;
        v1.tangent += tangent;
        v2.tangent += tangent;
        v0.bitangent += bitangent;
        v1.bitangent += bitangent;
        v2.bitangent += bitangent;
    }

    for (auto& vertex : model.vertices) {
        TSVec3f tangent = normalize(vertex.tangent - vertex.normal * dot(vertex.normal, vertex.tangent));
        TSVec3f normal = normalize(vertex.normal);
        if (dot(cross(normal, tangent), vertex.bitangent) < 0.0f) {
            tangent = tangent * -1.0f;
        }

        vertex.tangent = tangent;
        vertex.bitangent = normalize(cross(normal, tangent));
        vertex.normal = normal;
    }
}
} // namespace

SceneModelData loadSceneModel(const std::string& filePath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filePath.c_str())) {
        throw std::runtime_error(err);
    }

    SceneModelData model;
    std::unordered_map<SceneVertex, uint32_t> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            SceneVertex vertex{};
            if (index.vertex_index >= 0) {
                vertex.position = TSVec3f(
                    attrib.vertices[3 * index.vertex_index + 0] * 0.01f,
                    attrib.vertices[3 * index.vertex_index + 1] * 0.01f,
                    attrib.vertices[3 * index.vertex_index + 2] * 0.01f);
            }

            if (index.normal_index >= 0) {
                vertex.normal = TSVec3f(
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]);
            }

            if (index.texcoord_index >= 0) {
                vertex.texCoord = TSVec2f(
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]);
            }

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(model.vertices.size());
                model.vertices.push_back(vertex);
            }
            model.indices.push_back(uniqueVertices[vertex]);
        }
    }

    calculateTangentsAndBitangents(model);
    LOG_INFO("Loaded model: {} vertices, {} indices", model.vertices.size(), model.indices.size());
    return model;
}

const std::vector<SkyboxVertexData>& getSkyboxVertices() {
    static const std::vector<SkyboxVertexData> vertices = {
        {{-1.0f, -1.0f,  1.0f}}, {{ 1.0f, -1.0f,  1.0f}}, {{ 1.0f,  1.0f,  1.0f}}, {{-1.0f,  1.0f,  1.0f}},
        {{-1.0f, -1.0f, -1.0f}}, {{ 1.0f, -1.0f, -1.0f}}, {{ 1.0f,  1.0f, -1.0f}}, {{-1.0f,  1.0f, -1.0f}}
    };
    return vertices;
}

const std::vector<uint32_t>& getSkyboxIndices() {
    static const std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0,
        1, 5, 6, 6, 2, 1,
        5, 4, 7, 7, 6, 5,
        4, 0, 3, 3, 7, 4,
        3, 2, 6, 6, 7, 3,
        4, 5, 1, 1, 0, 4
    };
    return indices;
}

} // namespace Tasrovy::RHI
