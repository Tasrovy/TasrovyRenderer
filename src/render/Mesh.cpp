#include "Mesh.h"
#include "Material.h"
#include "Model.hpp"

namespace Tasrovy::Render {

std::shared_ptr<Mesh> Mesh::create(
    std::vector<MeshVertex> vertices,
    std::vector<uint32_t> indices,
    std::vector<Submesh> submeshes) {
    auto mesh = std::shared_ptr<Mesh>(new Mesh());
    mesh->vertices_ = std::move(vertices);
    mesh->indices_ = std::move(indices);
    mesh->submeshes_ = std::move(submeshes);
    mesh->buildVertexDescriptions();
    return mesh;
}

std::shared_ptr<Mesh> Mesh::fromModel(const Tasrovy::FS::Model& model) {
    auto mesh = std::shared_ptr<Mesh>(new Mesh());

    mesh->vertices_.reserve(model.GetVertices().size());
    for (const auto& v : model.GetVertices()) {
        MeshVertex mv;
        mv.position = v.position;
        mv.normal = v.normal;
        mv.tangent = v.tangent;
        mv.bitangent = v.bitangent;
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
        mesh->submeshes_.emplace_back(s.materialName, s.indexOffset, s.indexCount);
    }

    mesh->buildVertexDescriptions();
    return mesh;
}

void Mesh::setVertices(std::vector<MeshVertex> vertices) { vertices_ = std::move(vertices); }
void Mesh::setIndices(std::vector<uint32_t> indices) { indices_ = std::move(indices); }
void Mesh::setSubmeshes(std::vector<Submesh> submeshes) { submeshes_ = std::move(submeshes); }

void Mesh::setSubmeshMaterial(size_t submeshIndex, std::shared_ptr<Material> material) {
    if (submeshIndex < submeshes_.size()) {
        submeshes_[submeshIndex].setMaterial(std::move(material));
    }
}

const std::vector<MeshVertex>& Mesh::getVertices() const { return vertices_; }
const std::vector<uint32_t>& Mesh::getIndices() const { return indices_; }
const std::vector<Submesh>& Mesh::getSubmeshes() const { return submeshes_; }
std::vector<Submesh>& Mesh::getSubmeshes() { return submeshes_; }

std::shared_ptr<Material> Mesh::getSubmeshMaterial(size_t submeshIndex) const {
    if (submeshIndex < submeshes_.size()) {
        return submeshes_[submeshIndex].getMaterial();
    }
    return nullptr;
}

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
        { MeshVertexLocation::Position, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position) },
        { MeshVertexLocation::Normal, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal) },
        { MeshVertexLocation::Tangent, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, tangent) },
        { MeshVertexLocation::Bitangent, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, bitangent) },
        { MeshVertexLocation::UV0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv0) },
        { MeshVertexLocation::VertexColor, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, vertexColor) },
        { MeshVertexLocation::UV1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv1) },
        { MeshVertexLocation::UV2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv2) },
        { MeshVertexLocation::UV3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv3) },
    };
}

const std::vector<VkVertexInputBindingDescription>& Mesh::getVertexBindingDescription() {
    return vertexBindingDesc_;
}

const std::vector<VkVertexInputAttributeDescription>& Mesh::getVertexAttributeDescription() {
    return vertexAttrDesc_;
}

} // namespace Tasrovy::Render
