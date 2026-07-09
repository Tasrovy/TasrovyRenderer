#pragma once

#include <memory>
#include <cstdint>
#include <string>

namespace Tasrovy::RHI {

struct DescriptorImageInfo {
    uint64_t nativeSampler = 0;
    uint64_t nativeView = 0;
    uint32_t imageLayout = 0;
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
    static std::shared_ptr<Image> CreateTextureFromNative(void* ctx, void* submitter, const std::string& path, bool generateMips, uint32_t format);
    static std::shared_ptr<Image> CreateCubemapFromNative(void* ctx, void* submitter, const std::string& dirPath);
    static std::shared_ptr<Image> CreateAttachmentFromNative(void* ctx, uint32_t w, uint32_t h, uint32_t format, uint32_t samples);
    static std::shared_ptr<Image> CreateImage2DFromNative(void* ctx, uint32_t w, uint32_t h, uint32_t format);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
