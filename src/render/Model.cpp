#include "Model.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#include <stdexcept>
#include <iostream>
#include <unordered_map>

Model::Model(const std::string& filePath)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, filePath.c_str())) {
        throw std::runtime_error(err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            if (index.vertex_index >= 0) {
                vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0]*0.01,
                    attrib.vertices[3 * index.vertex_index + 1]*0.01,
                    attrib.vertices[3 * index.vertex_index + 2]*0.01
                };
            }

            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }

            if (index.texcoord_index >= 0) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1] // 翻转 V 轴（OBJ 通常需要）
                };
            }

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
	calculateTangentsAndBitangents();
    std::cout << "Loaded model: " << vertices.size() << " vertices, "
        << indices.size() << " indices\n";
    vertexBindingDescription = { 
        {
        0,
        sizeof(Vertex),
        VK_VERTEX_INPUT_RATE_VERTEX
        } 
    };
    vertexAttributeDescriptions = {
        {
            0,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex, position)
        },        
        {
            1,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex, normal)
        },
        {
            2,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex, tangent)
        },
        {
            3,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex, bitangent)
        },
        {
            4,
            0,
            VK_FORMAT_R32G32_SFLOAT,
            offsetof(Vertex, texCoord)
        }
    };
}

void Model::calculateTangentsAndBitangents() {
    for (size_t i = 0; i < indices.size(); i += 3) {
        // 获取一个三角形的三个顶点
        Vertex& v0 = vertices[indices[i + 0]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        // 计算边和UV增量
        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        glm::vec3 tangent, bitangent;

        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        // 将计算出的值累加到每个顶点上
        v0.tangent += tangent; v1.tangent += tangent; v2.tangent += tangent;
        v0.bitangent += bitangent; v1.bitangent += bitangent; v2.bitangent += bitangent;
    }

    // 对每个顶点的 TBN 向量进行正交化和归一化
    for (auto& vertex : vertices) {
        // Gram-Schmidt aorthonormalize
        glm::vec3 T = glm::normalize(vertex.tangent - vertex.normal * glm::dot(vertex.normal, vertex.tangent));
        glm::vec3 N = glm::normalize(vertex.normal);

        // 检查叉乘结果是否与 bitangent 方向一致，如果不一致则翻转
        if (glm::dot(glm::cross(N, T), vertex.bitangent) < 0.0f) {
            T = T * -1.0f;
        }

        vertex.tangent = T;
        vertex.bitangent = glm::normalize(glm::cross(N, T)); // 重新计算 bitangent 以确保正交
        vertex.normal = N;
    }
}

Model::~Model()
{
    vertices.clear();
    indices.clear();
    vertexBindingDescription.clear();
    vertexAttributeDescriptions.clear();
}