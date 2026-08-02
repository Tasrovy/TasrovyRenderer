#include "Texture.hpp"

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
    : filePath_(std::move(other.filePath_))
    , type_(other.type_)
    , generateMips_(other.generateMips_) {
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        filePath_ = std::move(other.filePath_);
        type_ = other.type_;
        generateMips_ = other.generateMips_;
    }
    return *this;
}

void Texture::loadFromFile(const std::string& path, bool generateMips) {
    generateMips_ = generateMips;
    type_ = Type::Texture2D;
    // Render owns only the asset description. The execution backend resolves
    // this path into a physical GPU texture.
    filePath_ = path;
}

void Texture::loadCubemap(const std::string& directoryPath) {
    type_ = Type::Cubemap;
    filePath_ = directoryPath;
}

Texture::Type Texture::getType() const { return type_; }
bool Texture::hasMipmaps() const { return generateMips_; }
const std::string& Texture::getFilePath() const { return filePath_; }

} // namespace Tasrovy::Render
