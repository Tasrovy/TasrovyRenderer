#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace Tasrovy::FS {
class Model;
}

namespace Tasrovy::Render {
class Mesh;
}

namespace Tasrovy::Assets {

struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    uint32_t arrayLayers = 1;
    bool cubemap = false;
    std::vector<uint8_t> pixels;
};

class RenderAssetFactory {
public:
    static std::shared_ptr<Tasrovy::Render::Mesh> meshFromModel(
        const Tasrovy::FS::Model& model);
    static DecodedImage decodeTexture(const std::string& path);
    static DecodedImage decodeCubemap(const std::string& directory);
};

} // namespace Tasrovy::Assets
