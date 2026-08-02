#include "RenderAssetFactory.h"

#include "../filesystem/Model.hpp"
#include "../filesystem/Image.hpp"
#include "../render/Mesh.h"
#include "../render/Submesh.h"

#include <vector>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace Tasrovy::Assets {

std::shared_ptr<Tasrovy::Render::Mesh>
RenderAssetFactory::meshFromModel(const Tasrovy::FS::Model& model) {
    std::vector<Tasrovy::Render::MeshVertex> vertices;
    vertices.reserve(model.GetVertices().size());
    for (const auto& vertex : model.GetVertices()) {
        vertices.push_back({
            vertex.position,
            vertex.normal,
            vertex.tangent,
            vertex.bitangent,
            vertex.vertexColor,
            vertex.uv0,
            vertex.uv1,
            vertex.uv2,
            vertex.uv3
        });
    }

    std::vector<Tasrovy::Render::Submesh> submeshes;
    submeshes.reserve(model.GetSubmeshes().size());
    for (const auto& submesh : model.GetSubmeshes()) {
        submeshes.emplace_back(
            submesh.materialName,
            submesh.indexOffset,
            submesh.indexCount);
    }

    return Tasrovy::Render::Mesh::create(
        std::move(vertices),
        model.GetIndices(),
        std::move(submeshes));
}

DecodedImage RenderAssetFactory::decodeTexture(
    const std::string& path) {
    Tasrovy::FS::Image source;
    if (!source.LoadFromFile(path, false, 4)) {
        throw std::runtime_error(
            "Failed to decode texture: " + path);
    }
    DecodedImage result;
    result.width = static_cast<uint32_t>(source.GetWidth());
    result.height = static_cast<uint32_t>(source.GetHeight());
    result.pixels.assign(
        source.GetData(),
        source.GetData() + source.GetDataSize());
    return result;
}

DecodedImage RenderAssetFactory::decodeCubemap(
    const std::string& directory) {
    static constexpr std::array<const char*, 6> FaceNames = {
        "right.png", "left.png", "top.png",
        "bottom.png", "front.png", "back.png"
    };
    DecodedImage result;
    result.arrayLayers = 6;
    result.cubemap = true;
    for (const auto* faceName : FaceNames) {
        const auto path =
            (std::filesystem::path(directory) / faceName).string();
        const auto face = decodeTexture(path);
        if (result.width == 0) {
            result.width = face.width;
            result.height = face.height;
        } else if (
            result.width != face.width ||
            result.height != face.height) {
            throw std::runtime_error(
                "Cubemap faces must share one extent: " + path);
        }
        result.pixels.insert(
            result.pixels.end(),
            face.pixels.begin(),
            face.pixels.end());
    }
    return result;
}

} // namespace Tasrovy::Assets
