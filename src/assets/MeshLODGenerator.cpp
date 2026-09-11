#include "MeshLODGenerator.h"

#include "../render/Mesh.h"
#include "../render/Submesh.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Tasrovy::Assets {
namespace {

using Tasrovy::Render::Mesh;
using Tasrovy::Render::MeshLOD;
using Tasrovy::Render::MeshVertex;
using Tasrovy::Render::Submesh;

struct SimplificationAttributes {
    std::array<float, 5> values{};
};

static_assert(offsetof(MeshVertex, position) == 0);

std::vector<uint32_t> copyIndexRange(
    const std::vector<uint32_t>& indices,
    uint32_t offset,
    uint32_t count) {
    const size_t begin = std::min<size_t>(offset, indices.size());
    const size_t end = std::min<size_t>(
        static_cast<size_t>(offset) + count, indices.size());
    return {indices.begin() + static_cast<std::ptrdiff_t>(begin),
            indices.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::vector<uint32_t> simplifyRange(
    const std::vector<uint32_t>& source,
    const std::vector<MeshVertex>& vertices,
    const std::vector<SimplificationAttributes>& attributes,
    size_t targetIndexCount,
    float targetError,
    float& resultError) {
    if (source.size() <= targetIndexCount || source.size() < 6) {
        resultError = 0.0f;
        return source;
    }

    std::vector<uint32_t> result(source.size());
    static constexpr std::array<float, 5> AttributeWeights = {
        0.5f, 0.5f, 0.5f, 0.25f, 0.25f
    };
    const auto* positions =
        reinterpret_cast<const float*>(&vertices.front().position);
    const size_t simplifiedCount = meshopt_simplifyWithAttributes(
        result.data(),
        source.data(),
        source.size(),
        positions,
        vertices.size(),
        sizeof(MeshVertex),
        attributes.front().values.data(),
        sizeof(SimplificationAttributes),
        AttributeWeights.data(),
        AttributeWeights.size(),
        nullptr,
        targetIndexCount,
        targetError,
        meshopt_SimplifyPermissive | meshopt_SimplifyLockBorder,
        &resultError);

    result.resize(simplifiedCount - simplifiedCount % 3);
    if (result.size() >= source.size()) {
        return source;
    }

    std::vector<uint32_t> cacheOptimized(result.size());
    meshopt_optimizeVertexCache(
        cacheOptimized.data(), result.data(), result.size(), vertices.size());
    return cacheOptimized;
}

} // namespace

void MeshLODGenerator::generate(
    const std::shared_ptr<Mesh>& mesh,
    const MeshLODSettings& settings) {
    if (!mesh || mesh->getVertices().empty() || mesh->getIndexCount() < 6) {
        return;
    }

    const auto& vertices = mesh->getVertices();
    const std::vector<uint32_t> lod0Indices(
        mesh->getIndices().begin(),
        mesh->getIndices().begin() +
            static_cast<std::ptrdiff_t>(mesh->getIndexCount()));
    if (std::any_of(
            lod0Indices.begin(), lod0Indices.end(),
            [&](uint32_t index) { return index >= vertices.size(); })) {
        return;
    }
    const auto lod0Submeshes = mesh->getSubmeshes();

    std::vector<SimplificationAttributes> attributes(vertices.size());
    for (size_t index = 0; index < vertices.size(); ++index) {
        const auto& vertex = vertices[index];
        attributes[index].values = {
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            vertex.uv0.x, vertex.uv0.y
        };
    }

    std::vector<std::vector<uint32_t>> currentRanges;
    if (lod0Submeshes.empty()) {
        currentRanges.push_back(lod0Indices);
    } else {
        currentRanges.reserve(lod0Submeshes.size());
        for (const auto& submesh : lod0Submeshes) {
            currentRanges.push_back(copyIndexRange(
                lod0Indices,
                submesh.getIndexOffset(),
                submesh.getIndexCount()));
        }
    }

    std::vector<uint32_t> combinedIndices = lod0Indices;
    std::vector<MeshLOD> lods;
    lods.push_back({
        0,
        static_cast<uint32_t>(lod0Indices.size()),
        0.0f,
        settings.minimumScreenCoverage.empty()
            ? 0.0f : settings.minimumScreenCoverage.front(),
        lod0Submeshes
    });

    const size_t levelCount = std::min(
        settings.triangleRatios.size(), settings.targetErrors.size());
    for (size_t level = 0; level < levelCount; ++level) {
        std::vector<std::vector<uint32_t>> nextRanges;
        nextRanges.reserve(currentRanges.size());
        size_t currentTotal = 0;
        size_t nextTotal = 0;
        float levelError = 0.0f;

        for (size_t rangeIndex = 0;
             rangeIndex < currentRanges.size(); ++rangeIndex) {
            const auto& current = currentRanges[rangeIndex];
            currentTotal += current.size();
            const size_t sourceTriangleCount = current.size() / 3;
            const size_t minimumTriangles = std::min<size_t>(
                settings.minimumTriangleCount, sourceTriangleCount);
            const size_t requestedTriangles = std::max<size_t>(
                minimumTriangles,
                static_cast<size_t>(
                    (lod0Submeshes.empty()
                         ? lod0Indices.size() / 3
                         : lod0Submeshes[rangeIndex].getIndexCount() / 3) *
                    std::clamp(settings.triangleRatios[level], 0.0f, 1.0f)));
            const size_t targetIndexCount = requestedTriangles * 3;
            float rangeError = 0.0f;
            auto simplified = simplifyRange(
                current,
                vertices,
                attributes,
                targetIndexCount,
                settings.targetErrors[level],
                rangeError);
            nextTotal += simplified.size();
            levelError = std::max(levelError, rangeError);
            nextRanges.push_back(std::move(simplified));
        }

        if (nextTotal >= currentTotal || nextTotal == 0) {
            break;
        }

        MeshLOD lod;
        lod.indexOffset = static_cast<uint32_t>(combinedIndices.size());
        lod.indexCount = static_cast<uint32_t>(nextTotal);
        lod.simplificationError = levelError;
        const size_t coverageIndex = level + 1;
        lod.minimumScreenCoverage =
            coverageIndex < settings.minimumScreenCoverage.size()
                ? settings.minimumScreenCoverage[coverageIndex]
                : 0.0f;

        for (size_t rangeIndex = 0;
             rangeIndex < nextRanges.size(); ++rangeIndex) {
            const uint32_t rangeOffset =
                static_cast<uint32_t>(combinedIndices.size());
            combinedIndices.insert(
                combinedIndices.end(),
                nextRanges[rangeIndex].begin(),
                nextRanges[rangeIndex].end());
            if (!lod0Submeshes.empty()) {
                Submesh submesh(
                    lod0Submeshes[rangeIndex].getMaterialName(),
                    rangeOffset,
                    static_cast<uint32_t>(nextRanges[rangeIndex].size()));
                submesh.setMaterial(lod0Submeshes[rangeIndex].getMaterial());
                lod.submeshes.push_back(std::move(submesh));
            }
        }

        lods.push_back(std::move(lod));
        currentRanges = std::move(nextRanges);
    }

    if (lods.size() > 1) {
        mesh->setLODChain(std::move(combinedIndices), std::move(lods));
    }
}

} // namespace Tasrovy::Assets
