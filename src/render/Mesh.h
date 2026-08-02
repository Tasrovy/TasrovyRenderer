#pragma once

#include "Submesh.h"
#include "TSVector.h"
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

namespace Tasrovy::Render {

class Material;

struct MeshVertex {
    TSVec3f position;
    TSVec3f normal;
    TSVec3f tangent;
    TSVec3f bitangent;
    TSVec3f vertexColor;
    TSVec2f uv0;
    TSVec2f uv1;
    TSVec2f uv2;
    TSVec2f uv3;
};

namespace MeshVertexLocation {
inline constexpr uint32_t Position = 0;
inline constexpr uint32_t Normal = 1;
inline constexpr uint32_t Tangent = 2;
inline constexpr uint32_t Bitangent = 3;
inline constexpr uint32_t UV0 = 4;
inline constexpr uint32_t VertexColor = 5;
inline constexpr uint32_t UV1 = 6;
inline constexpr uint32_t UV2 = 7;
inline constexpr uint32_t UV3 = 8;
}

enum class VertexElementFormat {
    Float2,
    Float3,
    Float4
};

enum class VertexInputRate {
    PerVertex,
    PerInstance
};

struct VertexBindingDescription {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexInputRate inputRate = VertexInputRate::PerVertex;
};

struct VertexAttributeDescription {
    uint32_t location = 0;
    uint32_t binding = 0;
    VertexElementFormat format = VertexElementFormat::Float3;
    uint32_t offset = 0;
};

class Mesh : public std::enable_shared_from_this<Mesh> {
public:
    static std::shared_ptr<Mesh> create(
        std::vector<MeshVertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<Submesh> submeshes = {});

    static std::shared_ptr<Mesh> createPlane();
    static std::shared_ptr<Mesh> createCube();
    static std::shared_ptr<Mesh> createSphere(
        uint32_t sectors = 32,
        uint32_t stacks = 16);

    void setVertices(std::vector<MeshVertex> vertices);
    void setIndices(std::vector<uint32_t> indices);
    void setSubmeshes(std::vector<Submesh> submeshes);
    void setSubmeshMaterial(size_t submeshIndex, std::shared_ptr<Material> material);

    const std::vector<MeshVertex>& getVertices() const;
    const std::vector<uint32_t>& getIndices() const;
    const std::vector<Submesh>& getSubmeshes() const;
    std::vector<Submesh>& getSubmeshes();
    std::shared_ptr<Material> getSubmeshMaterial(size_t submeshIndex) const;

    size_t getVertexCount() const;
    size_t getIndexCount() const;
    void setSourcePath(std::string sourcePath);
    const std::string& getSourcePath() const;

    void calculateTangents();

    static const std::vector<VertexBindingDescription>&
        getVertexBindingDescription();
    static const std::vector<VertexAttributeDescription>&
        getVertexAttributeDescription();

private:
    Mesh() = default;

    std::vector<MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Submesh> submeshes_;
    std::string sourcePath_;
};

} // namespace Tasrovy::Render
