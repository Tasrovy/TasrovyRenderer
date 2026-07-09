#include "Texture.hpp"
#include "VulkanImage.h"

namespace Tasrovy::Render {

std::shared_ptr<Texture> Texture::create() {
    return std::shared_ptr<Texture>(new Texture());
}

std::shared_ptr<Texture> Texture::createFromFile(const std::string& path, bool generateMips) {
    auto tex = std::shared_ptr<Texture>(new Texture());
    tex->loadFromFile(path, generateMips);
    return tex;
}

std::shared_ptr<Texture> Texture::createCubemap(const std::string& directoryPath) {
    auto tex = std::shared_ptr<Texture>(new Texture());
    tex->loadCubemap(directoryPath);
    return tex;
}

Texture::~Texture() = default;

Texture::Texture(Texture&& other) noexcept
    : image_(std::move(other.image_))
    , type_(other.type_)
    , generateMips_(other.generateMips_) {
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        image_ = std::move(other.image_);
        type_ = other.type_;
        generateMips_ = other.generateMips_;
    }
    return *this;
}

void Texture::loadFromFile(const std::string& path, bool generateMips) {
    generateMips_ = generateMips;
    type_ = Type::Texture2D;
    // VulkanContext �?ImmediateSubmitter 由调用方�?RHI 层传�?    // 此处仅保存路径，实际加载延迟�?RHI 绑定阶段
    filePath_ = path;
}

void Texture::loadCubemap(const std::string& directoryPath) {
    type_ = Type::Cubemap;
    filePath_ = directoryPath;
}

VulkanImage* Texture::getImage() const { return image_.get(); }
void Texture::setImage(std::unique_ptr<VulkanImage> image) { image_ = std::move(image); }
Texture::Type Texture::getType() const { return type_; }
bool Texture::hasMipmaps() const { return generateMips_; }
const std::string& Texture::getFilePath() const { return filePath_; }

} // namespace Tasrovy::Render
