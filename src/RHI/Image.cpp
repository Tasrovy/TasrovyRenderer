#include "Image.h"
#include "RHIConfig.h"
#include <algorithm>

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanImage.h"
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/ImmediateSubmitter.h"
#endif

namespace Tasrovy::RHI {

struct Image::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanImage> vkImage;
#endif
};

Image::~Image() = default;

std::shared_ptr<Image> Image::CreateTextureFromNative(
    void* ctx,
    void* submitter,
    const ImageUploadDesc& upload) {
#ifdef TASROVY_API_VULKAN
    auto* context = static_cast<VulkanContext*>(ctx);
    auto* subm = static_cast<ImmediateSubmitter*>(submitter);
    const auto vkFormat = upload.format == 0
        ? VK_FORMAT_R8G8B8A8_SRGB
        : static_cast<VkFormat>(upload.format);
    auto vkImg = upload.cubemap
        ? VulkanImage::createCubemap(
            *context,
            *subm,
            upload.pixels.data(),
            upload.pixels.size() / std::max(upload.arrayLayers, 1u),
            upload.width,
            upload.height,
            vkFormat)
        : VulkanImage::createTexture(
            *context,
            *subm,
            upload.pixels.data(),
            upload.pixels.size(),
            upload.width,
            upload.height,
            upload.generateMipmaps,
            vkFormat);
    auto img = std::shared_ptr<Image>(new Image());
    img->impl_ = std::make_unique<Impl>();
    img->impl_->vkImage = std::move(vkImg);
    return img;
#else
    return nullptr;
#endif
}

std::shared_ptr<Image> Image::CreateSolidTextureFromNative(
    void* ctx,
    void* submitter,
    const std::array<float, 4>& color,
    uint32_t format) {
#ifdef TASROVY_API_VULKAN
    auto* context = static_cast<VulkanContext*>(ctx);
    auto* subm = static_cast<ImmediateSubmitter*>(submitter);
    auto vkImg = VulkanImage::createSolidTexture(
        *context, *subm, color, static_cast<VkFormat>(format));
    auto img = std::shared_ptr<Image>(new Image());
    img->impl_ = std::make_unique<Impl>();
    img->impl_->vkImage = std::move(vkImg);
    return img;
#else
    return nullptr;
#endif
}

std::shared_ptr<Image> Image::CreateAttachmentFromNative(
    void* ctx,
    uint32_t w,
    uint32_t h,
    uint32_t format,
    uint32_t samples,
    bool storage) {
#ifdef TASROVY_API_VULKAN
    auto* context = static_cast<VulkanContext*>(ctx);
    VkExtent2D extent = { w, h };
    const auto vkFormat = static_cast<VkFormat>(format);
    const bool isDepth =
        vkFormat == VK_FORMAT_D32_SFLOAT ||
        vkFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        vkFormat == VK_FORMAT_D24_UNORM_S8_UINT;
    auto usage = isDepth
        ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (storage && !isDepth) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    auto vkImg = VulkanImage::createAttachment(*context, extent,
        vkFormat, usage,
        static_cast<VkSampleCountFlagBits>(samples));
    auto img = std::shared_ptr<Image>(new Image());
    img->impl_ = std::make_unique<Impl>();
    img->impl_->vkImage = std::move(vkImg);
    return img;
#else
    (void)storage;
    return nullptr;
#endif
}

std::shared_ptr<Image> Image::CreateVirtualShadowAtlasFromNative(
    void* ctx,
    uint32_t atlasSize,
    uint32_t format) {
#ifdef TASROVY_API_VULKAN
    auto* context = static_cast<VulkanContext*>(ctx);
    auto vkImg = VulkanImage::createVirtualShadowAtlas(
        *context,
        {atlasSize, atlasSize},
        static_cast<VkFormat>(format));
    auto img = std::shared_ptr<Image>(new Image());
    img->impl_ = std::make_unique<Impl>();
    img->impl_->vkImage = std::move(vkImg);
    return img;
#else
    (void)ctx;
    (void)atlasSize;
    (void)format;
    return nullptr;
#endif
}

std::shared_ptr<Image> Image::CreateImage2DFromNative(void* ctx, uint32_t w, uint32_t h, uint32_t format) {
#ifdef TASROVY_API_VULKAN
    auto* context = static_cast<VulkanContext*>(ctx);
    VkExtent2D extent = { w, h };
    auto vkImg = VulkanImage::createImage2D(*context, extent,
        static_cast<VkFormat>(format),
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    auto img = std::shared_ptr<Image>(new Image());
    img->impl_ = std::make_unique<Impl>();
    img->impl_->vkImage = std::move(vkImg);
    return img;
#else
    return nullptr;
#endif
}

uint64_t Image::getNativeImage() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkImage->getImage());
#else
    return 0;
#endif
}

void* Image::getNativeImageObject() const {
#ifdef TASROVY_API_VULKAN
    return impl_->vkImage.get();
#else
    return nullptr;
#endif
}

uint64_t Image::getNativeView() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkImage->getView());
#else
    return 0;
#endif
}

uint64_t Image::getNativeSampler() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkImage->getSampler());
#else
    return 0;
#endif
}

uint32_t Image::getFormat() const {
#ifdef TASROVY_API_VULKAN
    return static_cast<uint32_t>(impl_->vkImage->getFormat());
#else
    return 0;
#endif
}

uint32_t Image::getMipLevels() const {
#ifdef TASROVY_API_VULKAN
    return impl_->vkImage->getMipLevels();
#else
    return 0;
#endif
}

DescriptorImageInfo Image::getDescriptorInfo() const {
#ifdef TASROVY_API_VULKAN
    auto info = impl_->vkImage->getDescriptorInfo();
    return {
        reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        static_cast<uint32_t>(info.imageLayout)
    };
#else
    return {};
#endif
}

DescriptorImageInfo Image::getDescriptorInfoForStorage() const {
#ifdef TASROVY_API_VULKAN
    auto info = impl_->vkImage->getDescriptorInfoForStorage();
    return {
        reinterpret_cast<uint64_t>(info.sampler),
        reinterpret_cast<uint64_t>(info.imageView),
        static_cast<uint32_t>(info.imageLayout)
    };
#else
    return {};
#endif
}

} // namespace Tasrovy::RHI
