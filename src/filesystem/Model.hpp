#pragma once
#include <vector>
#include <string>
#include <memory>
#include "TSVector.h"
#include "TSMatrix.h"

namespace Tasrovy::FS {

struct Vertex {
    TSVec3f position;
    TSVec3f normal;
    TSVec3f tangent;
    TSVec3f vertexColor;
    TSVec2f uv0;
    TSVec2f uv1;
    TSVec2f uv2;
    TSVec2f uv3;
    TSVec3f bitangent;
};

struct Submesh {
    std::string materialName;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct Bone {
    std::string name;
    TSMat4f offsetMatrix = TSMat4f(1.0f);
};

class Model {
public:
    static std::unique_ptr<Model> GenCube();
    static std::unique_ptr<Model> GenPlane();
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
