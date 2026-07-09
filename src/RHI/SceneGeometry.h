#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "TSVector.h"

namespace Tasrovy::RHI {

struct SceneVertex {
    TSVec3f position;
    TSVec3f normal;
    TSVec3f tangent;
    TSVec3f bitangent;
    TSVec2f texCoord;

    bool operator==(const SceneVertex& other) const {
        return position == other.position && normal == other.normal && texCoord == other.texCoord;
    }
};

struct SceneModelData {
    std::vector<SceneVertex> vertices;
    std::vector<uint32_t> indices;
};

struct SkyboxVertexData {
    TSVec3f pos;
};

SceneModelData loadSceneModel(const std::string& filePath);

const std::vector<SkyboxVertexData>& getSkyboxVertices();
const std::vector<uint32_t>& getSkyboxIndices();

} // namespace Tasrovy::RHI

namespace std {
template<> struct hash<Tasrovy::RHI::SceneVertex> {
    size_t operator()(Tasrovy::RHI::SceneVertex const& vertex) const noexcept;
};
} // namespace std
