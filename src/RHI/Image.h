#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <array>
#include <vector>

namespace Tasrovy::RHI {

struct DescriptorImageInfo {
    uint64_t nativeSampler = 0;
    uint64_t nativeView = 0;
    uint32_t imageLayout = 0;
};

struct ImageUploadDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    uint32_t arrayLayers = 1;
    uint32_t format = 0;
    bool generateMipmaps = true;
    bool cubemap = false;
    std::vector<uint8_t> pixels;
};

class Image : public std::enable_shared_from_this<Image> {
public:
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    uint64_t getNativeImage() const;
    uint64_t getNativeView() const;
    uint64_t getNativeSampler() const;
    uint32_t getFormat() const;
    uint32_t getMipLevels() const;

    DescriptorImageInfo getDescriptorInfo() const;
    DescriptorImageInfo getDescriptorInfoForStorage() const;

private:
    friend class Device;
    Image() = default;
    void* getNativeImageObject() const;
    static std::shared_ptr<Image> CreateTextureFromNative(
        void* ctx,
        void* submitter,
        const ImageUploadDesc& upload);
    static std::shared_ptr<Image> CreateSolidTextureFromNative(
        void* ctx,
        void* submitter,
        const std::array<float, 4>& color,
        uint32_t format);
    static std::shared_ptr<Image> CreateAttachmentFromNative(
        void* ctx,
        uint32_t w,
        uint32_t h,
        uint32_t format,
        uint32_t samples,
        bool storage = false);
    static std::shared_ptr<Image> CreateVirtualShadowAtlasFromNative(
        void* ctx,
        uint32_t atlasSize,
        uint32_t format);
    static std::shared_ptr<Image> CreateImage2DFromNative(void* ctx, uint32_t w, uint32_t h, uint32_t format);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
