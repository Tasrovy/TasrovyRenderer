#pragma once

#include "TSVector.h"
#include <volk.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Tasrovy::FS {
class Model;
}

namespace Tasrovy::Render {

struct MeshVertex {
    TSVec3f position;
    TSVec3f normal;
    TSVec3f tangent;
    TSVec3f vertexColor;
    TSVec2f uv0;
    TSVec2f uv1;
    TSVec2f uv2;
    TSVec2f uv3;
};

struct MeshSubmesh {
    std::string materialName;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

class Mesh : public std::enable_shared_from_this<Mesh> {
public:
    static std::shared_ptr<Mesh> create(
        std::vector<MeshVertex> vertices,
        std::vector<uint32_t> indices,
        std::vector<MeshSubmesh> submeshes = {});

    static std::shared_ptr<Mesh> fromModel(const Tasrovy::FS::Model& model);

    void setVertices(std::vector<MeshVertex> vertices);
    void setIndices(std::vector<uint32_t> indices);
    void setSubmeshes(std::vector<MeshSubmesh> submeshes);

    const std::vector<MeshVertex>& getVertices() const;
    const std::vector<uint32_t>& getIndices() const;
    const std::vector<MeshSubmesh>& getSubmeshes() const;

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
    std::vector<MeshSubmesh> submeshes_;
    std::vector<VkVertexInputBindingDescription> vertexBindingDesc_;
    std::vector<VkVertexInputAttributeDescription> vertexAttrDesc_;
};

} // namespace Tasrovy::Render
