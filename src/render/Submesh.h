#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Tasrovy::Render {

class Material;

class Submesh {
public:
    Submesh() = default;
    Submesh(std::string materialName, uint32_t indexOffset, uint32_t indexCount);

    const std::string& getMaterialName() const;
    void setMaterialName(std::string materialName);

    uint32_t getIndexOffset() const;
    void setIndexOffset(uint32_t indexOffset);

    uint32_t getIndexCount() const;
    void setIndexCount(uint32_t indexCount);

    void setMaterial(std::shared_ptr<Material> material);
    std::shared_ptr<Material> getMaterial() const;

private:
    std::string materialName_;
    uint32_t indexOffset_ = 0;
    uint32_t indexCount_ = 0;
    std::shared_ptr<Material> material_;
};

} // namespace Tasrovy::Render
