#pragma once

#include "Submesh.h"
#include "TSVector.h"
#include <volk.h>
#include <vector>
#include <memory>
#include <cstdint>

namespace Tasrovy::FS {
class Model;
}

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

class Mesh : public std::enable_shared_from_this<Mesh> {
public:
    static std::shared_ptr<Mesh> create(
        std::vector<MeshVertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<Submesh> submeshes = {});

    static std::shared_ptr<Mesh> fromModel(const Tasrovy::FS::Model& model);

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

    void calculateTangents();

    const std::vector<VkVertexInputBindingDescription>& getVertexBindingDescription();
    const std::vector<VkVertexInputAttributeDescription>& getVertexAttributeDescription();

private:
    Mesh() = default;

    void buildVertexDescriptions();

    std::vector<MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Submesh> submeshes_;
    std::vector<VkVertexInputBindingDescription> vertexBindingDesc_;
    std::vector<VkVertexInputAttributeDescription> vertexAttrDesc_;
};

} // namespace Tasrovy::Render
