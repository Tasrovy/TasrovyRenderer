#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include "RHITypes.h"

namespace Tasrovy::RHI {

class CommandList;
class BackendAccess;
class IImageBackend;

struct ImageUploadDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    uint32_t arrayLayers = 1;
    Format format = Format::Unknown;
    bool generateMipmaps = true;
    bool cubemap = false;
    std::vector<uint8_t> pixels;
};

class Image : public std::enable_shared_from_this<Image> {
public:
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Format getFormat() const;
    uint32_t getMipLevels() const;

private:
    friend class Device;
    friend class CommandList;
    friend class BackendAccess;
    Image() = default;
    static std::shared_ptr<Image> CreateFromBackend(
        std::unique_ptr<IImageBackend> backend);
    IImageBackend& backend();
    const IImageBackend& backend() const;
    DescriptorImageInfo getDescriptorInfo() const;
    DescriptorImageInfo getDescriptorInfoForStorage() const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
