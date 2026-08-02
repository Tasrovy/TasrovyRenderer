#include "Mesh.h"
#include "Material.h"

#include <algorithm>
#include <cmath>

namespace Tasrovy::Render {

std::shared_ptr<Mesh> Mesh::create(
    std::vector<MeshVertex> vertices,
    std::vector<uint32_t> indices,
    std::vector<Submesh> submeshes) {
    auto mesh = std::shared_ptr<Mesh>(new Mesh());
    mesh->vertices_ = std::move(vertices);
    mesh->indices_ = std::move(indices);
    mesh->submeshes_ = std::move(submeshes);
    return mesh;
}

std::shared_ptr<Mesh> Mesh::createPlane() {
    const float half = 0.5f;
    const TSVec3f normal(0.0f, 1.0f, 0.0f);
    const TSVec3f tangent(1.0f, 0.0f, 0.0f);
    const TSVec3f bitangent(0.0f, 0.0f, -1.0f);
    const TSVec3f white(1.0f);
    const TSVec2f zero(0.0f);
    return create({
        {TSVec3f(-half, 0.0f, -half), normal, tangent, bitangent, white, TSVec2f(0.0f, 1.0f), zero, zero, zero},
        {TSVec3f(-half, 0.0f,  half), normal, tangent, bitangent, white, TSVec2f(0.0f, 0.0f), zero, zero, zero},
        {TSVec3f( half, 0.0f,  half), normal, tangent, bitangent, white, TSVec2f(1.0f, 0.0f), zero, zero, zero},
        {TSVec3f( half, 0.0f, -half), normal, tangent, bitangent, white, TSVec2f(1.0f, 1.0f), zero, zero, zero}
    }, {0, 1, 2, 0, 2, 3});
}

std::shared_ptr<Mesh> Mesh::createCube() {
    const float h = 0.5f;
    const TSVec3f white(1.0f);
    const TSVec2f zero(0.0f);
    const auto vertex = [&](TSVec3f position, TSVec3f normal, TSVec3f tangent, TSVec2f uv) {
        return MeshVertex{
            position, normal, tangent, cross(normal, tangent), white,
            uv, zero, zero, zero
        };
    };
    std::vector<MeshVertex> vertices = {
        vertex({-h,-h, h},{ 0, 0, 1},{ 1, 0, 0},{0,0}), vertex({ h,-h, h},{ 0, 0, 1},{ 1, 0, 0},{1,0}),
        vertex({ h, h, h},{ 0, 0, 1},{ 1, 0, 0},{1,1}), vertex({-h, h, h},{ 0, 0, 1},{ 1, 0, 0},{0,1}),
        vertex({ h,-h,-h},{ 0, 0,-1},{-1, 0, 0},{0,0}), vertex({-h,-h,-h},{ 0, 0,-1},{-1, 0, 0},{1,0}),
        vertex({-h, h,-h},{ 0, 0,-1},{-1, 0, 0},{1,1}), vertex({ h, h,-h},{ 0, 0,-1},{-1, 0, 0},{0,1}),
        vertex({ h,-h, h},{ 1, 0, 0},{ 0, 0, 1},{0,0}), vertex({ h,-h,-h},{ 1, 0, 0},{ 0, 0, 1},{1,0}),
        vertex({ h, h,-h},{ 1, 0, 0},{ 0, 0, 1},{1,1}), vertex({ h, h, h},{ 1, 0, 0},{ 0, 0, 1},{0,1}),
        vertex({-h,-h,-h},{-1, 0, 0},{ 0, 0,-1},{0,0}), vertex({-h,-h, h},{-1, 0, 0},{ 0, 0,-1},{1,0}),
        vertex({-h, h, h},{-1, 0, 0},{ 0, 0,-1},{1,1}), vertex({-h, h,-h},{-1, 0, 0},{ 0, 0,-1},{0,1}),
        vertex({-h, h, h},{ 0, 1, 0},{ 1, 0, 0},{0,0}), vertex({ h, h, h},{ 0, 1, 0},{ 1, 0, 0},{1,0}),
        vertex({ h, h,-h},{ 0, 1, 0},{ 1, 0, 0},{1,1}), vertex({-h, h,-h},{ 0, 1, 0},{ 1, 0, 0},{0,1}),
        vertex({-h,-h,-h},{ 0,-1, 0},{ 1, 0, 0},{0,0}), vertex({ h,-h,-h},{ 0,-1, 0},{ 1, 0, 0},{1,0}),
        vertex({ h,-h, h},{ 0,-1, 0},{ 1, 0, 0},{1,1}), vertex({-h,-h, h},{ 0,-1, 0},{ 1, 0, 0},{0,1})
    };
    return create(std::move(vertices), {
        0,1,2,2,3,0, 4,5,6,6,7,4, 8,9,10,10,11,8,
        12,13,14,14,15,12, 16,17,18,18,19,16, 20,21,22,22,23,20
    });
}

std::shared_ptr<Mesh> Mesh::createSphere(
    uint32_t sectors,
    uint32_t stacks) {
    sectors = std::max(3u, sectors);
    stacks = std::max(2u, stacks);
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(sectors) * stacks * 4u);
    indices.reserve(static_cast<size_t>(sectors) * stacks * 6u);
    const TSVec2f zero(0.0f);
    const TSVec3f white(1.0f);
    const auto point = [](float theta, float phi) {
        return TSVec3f(
            std::sin(phi) * std::cos(theta),
            std::cos(phi),
            std::sin(phi) * std::sin(theta));
    };
    for (uint32_t sector = 0; sector < sectors; ++sector) {
        for (uint32_t stack = 0; stack < stacks; ++stack) {
            const float u0 = static_cast<float>(sector) / sectors;
            const float u1 = static_cast<float>(sector + 1u) / sectors;
            const float v0 = static_cast<float>(stack) / stacks;
            const float v1 = static_cast<float>(stack + 1u) / stacks;
            const float theta0 = u0 * two_pi<float>();
            const float theta1 = u1 * two_pi<float>();
            const float phi0 = v0 * pi<float>();
            const float phi1 = v1 * pi<float>();
            const TSVec3f positions[] = {
                point(theta0, phi0), point(theta1, phi0),
                point(theta0, phi1), point(theta1, phi1)
            };
            const TSVec2f uvs[] = {{u0,v0},{u1,v0},{u0,v1},{u1,v1}};
            const float thetas[] = {theta0, theta1, theta0, theta1};
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (uint32_t index = 0; index < 4; ++index) {
                TSVec3f tangent(
                    -std::sin(thetas[index]), 0.0f,
                    std::cos(thetas[index]));
                tangent = normalize(tangent);
                vertices.push_back({
                    positions[index], positions[index], tangent,
                    cross(positions[index], tangent), white,
                    uvs[index], zero, zero, zero
                });
            }
            indices.insert(indices.end(), {
                base, base + 1u, base + 2u,
                base + 1u, base + 3u, base + 2u
            });
        }
    }
    return create(std::move(vertices), std::move(indices));
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
void Mesh::setSourcePath(std::string sourcePath) { sourcePath_ = std::move(sourcePath); }
const std::string& Mesh::getSourcePath() const { return sourcePath_; }

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

const std::vector<VertexBindingDescription>&
Mesh::getVertexBindingDescription() {
    static const std::vector<VertexBindingDescription> descriptions = {{
        0,
        sizeof(MeshVertex),
        VertexInputRate::PerVertex
    }};
    return descriptions;
}

const std::vector<VertexAttributeDescription>&
Mesh::getVertexAttributeDescription() {
    static const std::vector<VertexAttributeDescription> descriptions = {
        { MeshVertexLocation::Position, 0, VertexElementFormat::Float3, offsetof(MeshVertex, position) },
        { MeshVertexLocation::Normal, 0, VertexElementFormat::Float3, offsetof(MeshVertex, normal) },
        { MeshVertexLocation::Tangent, 0, VertexElementFormat::Float3, offsetof(MeshVertex, tangent) },
        { MeshVertexLocation::Bitangent, 0, VertexElementFormat::Float3, offsetof(MeshVertex, bitangent) },
        { MeshVertexLocation::UV0, 0, VertexElementFormat::Float2, offsetof(MeshVertex, uv0) },
        { MeshVertexLocation::VertexColor, 0, VertexElementFormat::Float3, offsetof(MeshVertex, vertexColor) },
        { MeshVertexLocation::UV1, 0, VertexElementFormat::Float2, offsetof(MeshVertex, uv1) },
        { MeshVertexLocation::UV2, 0, VertexElementFormat::Float2, offsetof(MeshVertex, uv2) },
        { MeshVertexLocation::UV3, 0, VertexElementFormat::Float2, offsetof(MeshVertex, uv3) },
    };
    return descriptions;
}

} // namespace Tasrovy::Render
