#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

namespace Tasrovy {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 vertexColor;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec2 uv2;
    glm::vec2 uv3;
};

struct Submesh {
    std::string materialName;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct Bone {
    std::string name;
    glm::mat4 offsetMatrix = glm::mat4(1.0f);
};

class Model {
public:
    static std::unique_ptr<Model> GenCube();
    static std::unique_ptr<Model> GenSphere(uint32_t sectors = 32, uint32_t stacks = 16);

    Model() = default;
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    ~Model() = default;

    void SetData(std::vector<Vertex> verts, std::vector<uint32_t> inds);

    const std::vector<Vertex>& GetVertices() const { return vertices; }
    const std::vector<uint32_t>& GetIndices() const { return indices; }
    const std::vector<Submesh>& GetSubmeshes() const { return submeshes; }
    const std::vector<Bone>& GetBones() const { return bones; }

    std::vector<Vertex>& GetVertices() { return vertices; }
    std::vector<uint32_t>& GetIndices() { return indices; }
    std::vector<Submesh>& GetSubmeshes() { return submeshes; }
    std::vector<Bone>& GetBones() { return bones; }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
    std::vector<Bone> bones;
};

}
