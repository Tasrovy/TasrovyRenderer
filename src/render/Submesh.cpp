#include "Submesh.h"

#include <utility>

namespace Tasrovy::Render {

Submesh::Submesh(std::string materialName, uint32_t indexOffset, uint32_t indexCount)
    : materialName_(std::move(materialName))
    , indexOffset_(indexOffset)
    , indexCount_(indexCount) {
}

const std::string& Submesh::getMaterialName() const {
    return materialName_;
}

void Submesh::setMaterialName(std::string materialName) {
    materialName_ = std::move(materialName);
}

uint32_t Submesh::getIndexOffset() const {
    return indexOffset_;
}

void Submesh::setIndexOffset(uint32_t indexOffset) {
    indexOffset_ = indexOffset;
}

uint32_t Submesh::getIndexCount() const {
    return indexCount_;
}

void Submesh::setIndexCount(uint32_t indexCount) {
    indexCount_ = indexCount;
}

void Submesh::setMaterial(std::shared_ptr<Material> material) {
    material_ = std::move(material);
}

std::shared_ptr<Material> Submesh::getMaterial() const {
    return material_;
}

} // namespace Tasrovy::Render
