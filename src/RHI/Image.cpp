#include "Image.h"
#include "ResourceBackend.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI {

struct Image::Impl {
    std::unique_ptr<IImageBackend> backend;
};

Image::~Image() = default;

std::shared_ptr<Image> Image::CreateFromBackend(
    std::unique_ptr<IImageBackend> backend) {
    if (!backend) throw std::invalid_argument("Image backend is null");
    auto image = std::shared_ptr<Image>(new Image());
    image->impl_ = std::make_unique<Impl>();
    image->impl_->backend = std::move(backend);
    return image;
}

IImageBackend& Image::backend() { return *impl_->backend; }
const IImageBackend& Image::backend() const { return *impl_->backend; }
Format Image::getFormat() const { return backend().format(); }
uint32_t Image::getMipLevels() const { return backend().mipLevels(); }
DescriptorImageInfo Image::getDescriptorInfo() const {
    return backend().descriptorInfo();
}
DescriptorImageInfo Image::getDescriptorInfoForStorage() const {
    return backend().storageDescriptorInfo();
}

} // namespace Tasrovy::RHI
