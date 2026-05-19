#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <functional>
#include <vulkan/vulkan.h>
#include <array>

struct Vertex{
    glm::vec3 position;
    glm::vec3 normal;
	glm::vec3 tangent;
	glm::vec3 bitangent;
    glm::vec2 texCoord;

    bool operator==(const Vertex& other) const {
        return position == other.position && normal == other.normal&&texCoord == other.texCoord;
    }
};

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const noexcept {
            size_t seed = 0;

            // 一个通用的 hash 组合方法（boost::hash_combine 的思路）
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
    };
}


class Model {
public:
    Model(const std::string& filePath);
    ~Model();
    std::vector<Vertex>& getVertices() { return vertices; }
    std::vector<uint32_t>& getIndices() { return indices; }
    std::vector<VkVertexInputBindingDescription>& getVertexBindingDescription(){ return vertexBindingDescription; }
    std::vector<VkVertexInputAttributeDescription>& getVertexAttributeDescription(){ return vertexAttributeDescriptions; }
    void calculateTangentsAndBitangents();
private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<VkVertexInputBindingDescription> vertexBindingDescription;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
};

struct SkyboxVertex {
    glm::vec3 pos;
    static std::array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions;

        // 属性 0: 位置 (pos)
        attributeDescriptions[0].binding = 0; // 这个属性属于哪个绑定 (见下文)
        attributeDescriptions[0].location = 0; // 着色器中的 layout(location = 0)
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // 格式是 3个32位浮点数
        attributeDescriptions[0].offset = offsetof(SkyboxVertex, pos); // 数据在这个结构体中的偏移量

        return attributeDescriptions;
    }

    static std::array<VkVertexInputBindingDescription, 1> getBindingDescriptions() {
        std::array<VkVertexInputBindingDescription, 1> bindingDescriptions{};

        bindingDescriptions[0].binding = 0; // 对应 vkCmdBindVertexBuffers 中的第一个绑定点
        bindingDescriptions[0].stride = sizeof(SkyboxVertex); // 每读取一个顶点，指针应该前进多少字节
        bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // 数据是逐顶点更新的

        return bindingDescriptions;
    }
};

const std::vector<SkyboxVertex> skyboxVertices = {
    {{-1.0f, -1.0f,  1.0f}}, {{ 1.0f, -1.0f,  1.0f}}, {{ 1.0f,  1.0f,  1.0f}}, {{-1.0f,  1.0f,  1.0f}},
    {{-1.0f, -1.0f, -1.0f}}, {{ 1.0f, -1.0f, -1.0f}}, {{ 1.0f,  1.0f, -1.0f}}, {{-1.0f,  1.0f, -1.0f}}
};

const std::vector<uint32_t> skyboxIndices = {
    0, 1, 2, 2, 3, 0, // front
    1, 5, 6, 6, 2, 1, // right
    5, 4, 7, 7, 6, 5, // back
    4, 0, 3, 3, 7, 4, // left
    3, 2, 6, 6, 7, 3, // top
    4, 5, 1, 1, 0, 4  // bottom
};