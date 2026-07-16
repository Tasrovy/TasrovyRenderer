#include "UVDebugMesh.h"

#include "Submesh.h"
#include <algorithm>

namespace Tasrovy::Render {

std::shared_ptr<Mesh> UVDebugMesh::createFromMesh(const Mesh& source, float panelSpacing) {
    const auto& sourceVertices = source.getVertices();
    const auto& sourceIndices = source.getIndices();
    const auto& sourceSubmeshes = source.getSubmeshes();

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;

    const size_t panelCount = std::max<size_t>(1, sourceSubmeshes.empty() ? 1 : sourceSubmeshes.size());
    const float firstPanelX = -0.5f * panelSpacing * static_cast<float>(panelCount - 1);

    auto appendVertex = [&](uint32_t sourceIndex, float panelX) {
        MeshVertex vertex{};
        if (sourceIndex < sourceVertices.size()) {
            vertex = sourceVertices[sourceIndex];
        }

        vertex.position = TSVec3f(
            panelX + vertex.uv0.x * 2.0f - 1.0f,
            vertex.uv0.y * 2.0f - 1.0f,
            0.0f);
        vertex.normal = TSVec3f(0.0f, 0.0f, 1.0f);
        vertex.tangent = TSVec3f(1.0f, 0.0f, 0.0f);
        vertex.bitangent = TSVec3f(0.0f, 1.0f, 0.0f);
        return vertex;
    };

    auto appendRange = [&](const std::string& materialName, uint32_t indexOffset, uint32_t indexCount, size_t panelIndex) {
        const uint32_t newIndexOffset = static_cast<uint32_t>(indices.size());
        const float panelX = firstPanelX + panelSpacing * static_cast<float>(panelIndex);

        const uint32_t end = std::min<uint32_t>(
            static_cast<uint32_t>(sourceIndices.size()),
            indexOffset + indexCount);
        for (uint32_t i = indexOffset; i + 2 < end; i += 3) {
            const auto v0 = appendVertex(sourceIndices[i + 0], panelX);
            const auto v1 = appendVertex(sourceIndices[i + 1], panelX);
            const auto v2 = appendVertex(sourceIndices[i + 2], panelX);

            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            vertices.push_back(v2);
            vertices.push_back(v1);
            vertices.push_back(v0);

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 4);
            indices.push_back(base + 5);
        }

        Submesh submesh(materialName, newIndexOffset, static_cast<uint32_t>(indices.size()) - newIndexOffset);
        if (panelIndex < sourceSubmeshes.size()) {
            submesh.setMaterial(sourceSubmeshes[panelIndex].getMaterial());
        }
        submeshes.push_back(std::move(submesh));
    };

    if (sourceSubmeshes.empty()) {
        appendRange("UV0", 0, static_cast<uint32_t>(sourceIndices.size()), 0);
    } else {
        for (size_t i = 0; i < sourceSubmeshes.size(); ++i) {
            const auto& submesh = sourceSubmeshes[i];
            appendRange(submesh.getMaterialName(), submesh.getIndexOffset(), submesh.getIndexCount(), i);
        }
    }

    return Mesh::create(std::move(vertices), std::move(indices), std::move(submeshes));
}

} // namespace Tasrovy::Render
