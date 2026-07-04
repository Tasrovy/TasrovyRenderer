#include "Mesh.h"
#include "Model.hpp"

namespace Tasrovy {

std::shared_ptr<Mesh> Mesh::create(
    std::vector<MeshVertex> vertices,
    std::vector<uint32_t> indices,
    std::vector<MeshSubmesh> submeshes) {
    auto mesh = std::shared_ptr<Mesh>(new Mesh());
    mesh->vertices_ = std::move(vertices);
    mesh->indices_ = std::move(indices);
    mesh->submeshes_ = std::move(submeshes);
    mesh->buildVertexDescriptions();
    return mesh;
}

std::shared_ptr<Mesh> Mesh::fromModel(const Model& model) {
    auto mesh = std::shared_ptr<Mesh>(new Mesh());

    mesh->vertices_.reserve(model.GetVertices().size());
    for (const auto& v : model.GetVertices()) {
        MeshVertex mv;
        mv.position = v.position;
        mv.normal = v.normal;
        mv.tangent = v.tangent;
        mv.vertexColor = v.vertexColor;
        mv.uv0 = v.uv0;
        mv.uv1 = v.uv1;
        mv.uv2 = v.uv2;
        mv.uv3 = v.uv3;
        mesh->vertices_.push_back(mv);
    }

    mesh->indices_ = model.GetIndices();

    mesh->submeshes_.reserve(model.GetSubmeshes().size());
    for (const auto& s : model.GetSubmeshes()) {
        MeshSubmesh ms;
        ms.materialName = s.materialName;
        ms.indexOffset = s.indexOffset;
        ms.indexCount = s.indexCount;
        mesh->submeshes_.push_back(ms);
    }

    mesh->buildVertexDescriptions();
    return mesh;
}

void Mesh::setVertices(std::vector<MeshVertex> vertices) { vertices_ = std::move(vertices); }
void Mesh::setIndices(std::vector<uint32_t> indices) { indices_ = std::move(indices); }
void Mesh::setSubmeshes(std::vector<MeshSubmesh> submeshes) { submeshes_ = std::move(submeshes); }

const std::vector<MeshVertex>& Mesh::getVertices() const { return vertices_; }
const std::vector<uint32_t>& Mesh::getIndices() const { return indices_; }
const std::vector<MeshSubmesh>& Mesh::getSubmeshes() const { return submeshes_; }

size_t Mesh::getVertexCount() const { return vertices_.size(); }
size_t Mesh::getIndexCount() const { return indices_.size(); }

void Mesh::calculateTangents() {
    for (size_t i = 0; i < indices_.size(); i += 3) {
        MeshVertex& v0 = vertices_[indices_[i + 0]];
        MeshVertex& v1 = vertices_[indices_[i + 1]];
        MeshVertex& v2 = vertices_[indices_[i + 2]];

        TSVec3f edge1 = v1.position - v0.position;
        TSVec3f edge2 = v2.position - v0.position;
        TSVec2f deltaUV1 = v1.uv0 - v0.uv0;
        TSVec2f deltaUV2 = v2.uv0 - v0.uv0;

        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(det) < 1e-8f) continue;

        float f = 1.0f / det;

        TSVec3f tangent;
        tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        TSVec3f bitangent;
        bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        v0.tangent = v0.tangent + tangent;
        v1.tangent = v1.tangent + tangent;
        v2.tangent = v2.tangent + tangent;
    }

    for (auto& vertex : vertices_) {
        TSVec3f T = normalize(vertex.tangent - vertex.normal * dot(vertex.normal, vertex.tangent));
        TSVec3f N = normalize(vertex.normal);

        if (dot(cross(N, T), vertex.tangent) < 0.0f) {
            T = T * -1.0f;
        }

        vertex.tangent = T;
        vertex.normal = N;
    }
}

void Mesh::buildVertexDescriptions() {
    vertexBindingDesc_ = {{
        0,
        sizeof(MeshVertex),
        VK_VERTEX_INPUT_RATE_VERTEX
    }};

    vertexAttrDesc_ = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal) },
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, tangent) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, vertexColor) },
        { 4, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(MeshVertex, uv0) },
        { 5, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(MeshVertex, uv1) },
        { 6, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(MeshVertex, uv2) },
        { 7, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(MeshVertex, uv3) },
    };
}

const std::vector<VkVertexInputBindingDescription>& Mesh::getVertexBindingDescription() {
    return vertexBindingDesc_;
}

const std::vector<VkVertexInputAttributeDescription>& Mesh::getVertexAttributeDescription() {
    return vertexAttrDesc_;
}

} // namespace Tasrovy
