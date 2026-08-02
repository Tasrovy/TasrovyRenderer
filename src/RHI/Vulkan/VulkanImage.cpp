#include "VulkanImage.h"
#include "ImmediateSubmitter.h"
#include "VulkanBuffer.h"
#include "../ResourceTracker.h"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <vector>
#include <Logger.hpp>

// --- 统一的私有构造函数 ---
VulkanImage::VulkanImage(VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkImageCreateFlags createFlags, uint32_t arrayLayers)
    : _context(&context), _format(format), _extent(extent), _layout(VK_IMAGE_LAYOUT_UNDEFINED), _mipLevels(mipLevels), _msaaCount(numSamples), _imageCreateFlags(createFlags)
{
    if (_msaaCount > VK_SAMPLE_COUNT_1_BIT && _mipLevels > 1) {
        throw std::runtime_error("VulkanImage Error: Multisampled images cannot have more than 1 mip level.");
    }
    if (arrayLayers != 1 && arrayLayers != 6) {
        throw std::runtime_error("VulkanImage Error: Array layers must be 1 (for 2D) or 6 (for Cubemap).");
    }
    if (arrayLayers == 6 && !(createFlags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)) {
        throw std::runtime_error("VulkanImage Error: Image with 6 layers must be created with CUBE_COMPATIBLE flag.");
    }

    _context->createImage(extent.width, extent.height, mipLevels, numSamples, format, VK_IMAGE_TILING_OPTIMAL, usage, properties, _image, _memory, createFlags, arrayLayers);
    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(_context->getDevice(), _image, &memoryRequirements);
    _allocationSize = memoryRequirements.size;
    Tasrovy::RHI::ResourceTracker::created(
        Tasrovy::RHI::TrackedResourceKind::Image,
        static_cast<uint64_t>(_allocationSize));
}

VulkanImage::~VulkanImage() {
    if (_context == nullptr) return;

    VkSampler sampler = _sampler;
    VkImageView view = _view;
    VkImage image = _image;
    VkDeviceMemory memory = _memory;
    const VkDeviceSize allocationSize = _allocationSize;
    _sampler = VK_NULL_HANDLE;
    _view = VK_NULL_HANDLE;
    _image = VK_NULL_HANDLE;
    _memory = VK_NULL_HANDLE;
    _allocationSize = 0;

    _context->deferDelete([sampler, view, image, memory, allocationSize](VkDevice device) {
        if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
        if (image != VK_NULL_HANDLE || memory != VK_NULL_HANDLE) {
            Tasrovy::RHI::ResourceTracker::destroyed(
                Tasrovy::RHI::TrackedResourceKind::Image,
                static_cast<uint64_t>(allocationSize));
        }
    });
}
// --- 静态工厂函数 ---

std::unique_ptr<VulkanImage> VulkanImage::createTexture(
    VulkanContext& context,
    ImmediateSubmitter& uploader,
    const void* pixels,
    size_t pixelBytes,
    uint32_t texWidth,
    uint32_t texHeight,
    bool generateMipmaps,
    VkFormat format) {
    if (!pixels || pixelBytes == 0 || texWidth == 0 || texHeight == 0) {
        throw std::runtime_error("invalid texture upload data");
    }
    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(pixelBytes);

    VulkanBuffer stagingBuffer(context, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.setData(pixels, imageSize, 0);

    VkExtent2D extent = {texWidth, texHeight};
    uint32_t mipLevels = generateMipmaps ? static_cast<uint32_t>(floor(log2(std::max(texWidth, texHeight)))) + 1 : 1;

    auto textureImage = std::make_unique<VulkanImage>(
        context, extent, format,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        mipLevels, VK_SAMPLE_COUNT_1_BIT, 0, 1
    );

    uploader.submit([&](VkCommandBuffer cmd) {
        textureImage->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = {
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            1
        };
        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer.getBuffer(),
            textureImage->getImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        if (generateMipmaps) {
            textureImage->recordGenerateMipmaps(cmd);
        }
        else {
            textureImage->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        });

    textureImage->createImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
    textureImage->createSampler();

    return textureImage;
}

std::unique_ptr<VulkanImage> VulkanImage::createSolidTexture(
    VulkanContext& context,
    ImmediateSubmitter& uploader,
    const std::array<float, 4>& color,
    VkFormat format) {
    auto textureImage = std::make_unique<VulkanImage>(
        context,
        VkExtent2D{1, 1},
        format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        0,
        1);

    uploader.submit([&](VkCommandBuffer cmd) {
        textureImage->recordTransitionLayout(
            cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue clear{};
        clear.float32[0] = color[0];
        clear.float32[1] = color[1];
        clear.float32[2] = color[2];
        clear.float32[3] = color[3];
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        vkCmdClearColorImage(
            cmd,
            textureImage->getImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear,
            1,
            &range);
        textureImage->recordTransitionLayout(
            cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    textureImage->createImageView(
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
    textureImage->createSampler();
    return textureImage;
}

std::unique_ptr<VulkanImage> VulkanImage::createCubemap(
    VulkanContext& context,
    ImmediateSubmitter& uploader,
    const void* pixels,
    size_t faceBytes,
    uint32_t texWidth,
    uint32_t texHeight,
    VkFormat format) {
    if (!pixels || faceBytes == 0 || texWidth == 0 || texHeight == 0) {
        throw std::runtime_error("invalid cubemap upload data");
    }
    const auto* facePixels = static_cast<const uint8_t*>(pixels);
    const VkDeviceSize faceSize =
        static_cast<VkDeviceSize>(faceBytes);
    VulkanBuffer stagingBuffer(context, faceSize * 6, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    for (uint32_t i = 0; i < 6; ++i) {
        stagingBuffer.setData(
            facePixels + i * faceBytes,
            faceBytes,
            i * faceBytes);
    }

    auto cubemapImage = std::make_unique<VulkanImage>(
        context, VkExtent2D{ (uint32_t)texWidth, (uint32_t)texHeight }, format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, 6
    );

    uploader.submit([&](VkCommandBuffer cmd) {
        cubemapImage->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        std::vector<VkBufferImageCopy> bufferCopyRegions;
        for (uint32_t i = 0; i < 6; i++) {
            VkBufferImageCopy region{};
            region.bufferOffset = i * faceSize;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = i;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 };
            bufferCopyRegions.push_back(region);
        }
        vkCmdCopyBufferToImage(cmd, stagingBuffer.getBuffer(), cubemapImage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

        cubemapImage->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    cubemapImage->createImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    cubemapImage->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    return cubemapImage;
}

std::unique_ptr<VulkanImage> VulkanImage::createAttachment(VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples) {
    // The sampled view exposes depth only. Layout barriers still cover both
    // aspects for combined depth/stencil formats when separate layouts are
    // unavailable.
    VkImageAspectFlags aspectFlags = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    auto attachmentImage = std::make_unique<VulkanImage>(
        context, extent, format, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, samples, 0, 1
    );

    attachmentImage->createImageView(aspectFlags, VK_IMAGE_VIEW_TYPE_2D);
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        attachmentImage->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }
    LOG_INFO("Created attachment image with format: {}", static_cast<int>(format));
    return attachmentImage;
}

std::unique_ptr<VulkanImage> VulkanImage::createVirtualShadowAtlas(
    VulkanContext& context,
    VkExtent2D extent,
    VkFormat format) {
    auto atlas = std::make_unique<VulkanImage>(
        context,
        extent,
        format,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        0,
        1);
    atlas->createImageView(VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D);
    atlas->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    LOG_INFO(
        "Created virtual shadow atlas {}x{} format {}",
        extent.width,
        extent.height,
        static_cast<int>(format));
    return atlas;
}

std::unique_ptr<VulkanImage> VulkanImage::createCube(
    VulkanContext& context,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    uint32_t mipLevels)
{
    // 1. 调用统一的私有构造函数来创建图像资源
    auto cubeImage = std::unique_ptr<VulkanImage>(new VulkanImage(
        context,
        extent,
        format,
        // 确保至少有 SAMPLED 和 STORAGE 用途，因为我们将从中采样并向其写入
        usage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        mipLevels,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        6 // 6 个数组层
    ));

    // 2. 为这个立方体图像创建视图和采样器
    cubeImage->createImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    cubeImage->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    return cubeImage;
}

// --- 成员函数 ---
void VulkanImage::recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout) {
    if (_layout == newLayout) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = _layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _image;
    barrier.subresourceRange.aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT; // Simplified
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = _mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = (_imageCreateFlags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ? 6 : 1; // Needs create flags

    VkPipelineStageFlags sourceStage;
    VkAccessFlags sourceAccessMask;
    VkPipelineStageFlags destinationStage;
    VkAccessFlags destinationAccessMask;
    // ... Simplified, you'd need a helper function here for all transitions ...
    // This part is complex, for a full implementation see the ImmediateSubmitter example
    if (_layout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        sourceAccessMask = 0; destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; destinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else { // Add more cases as needed
        sourceAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; destinationAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    barrier.srcAccessMask = sourceAccessMask;
    barrier.dstAccessMask = destinationAccessMask;
    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    _layout = newLayout;
}

void VulkanImage::recordGenerateMipmaps(VkCommandBuffer cmd) {
    // 如果只有一个 mip level，则无需生成，直接转换布局后返回
    if (_mipLevels <= 1) {
        // 确保最终布局是 SHADER_READ_ONLY_OPTIMAL
        recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return;
    }

    // 检查物理设备是否支持我们需要的格式特性（线性过滤）
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(_context->getPhysicalDevice(), _format, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting for mipmap generation!");
    }

    // 创建一个可复用的 Image Memory Barrier
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = _image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = _extent.width;
    int32_t mipHeight = _extent.height;

    // 循环遍历所有 mip levels，从 level 1 开始（level 0 是原始图像）
    for (uint32_t i = 1; i < _mipLevels; i++) {
        // --- 1. 将上一级 mip (i-1) 从“传输目标”转换为“传输源” ---
        // 这是 blit 操作的源
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        // --- 2. 执行 Blit 操作 ---
        // 将上一级 mip (i-1) 的内容，缩小并拷贝到当前级 mip (i)
        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(
            cmd,
            _image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // Source
            _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // Destination
            1, &blit,
            VK_FILTER_LINEAR // 使用线性插值以获得更好的缩小效果
        );

        // --- 3. 将上一级 mip (i-1) 从“传输源”转换为“着色器只读” ---
        // 因为上一级 mip 已经完成了它的 blit 使命，现在可以准备给着色器使用了
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // 目标是片段着色器
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        // 更新下一轮循环的尺寸
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // --- 4. 转换最后一级 mip 的布局 ---
    // 循环结束后，最后一级 mip (_mipLevels - 1) 还处于 TRANSFER_DST 布局
    // 我们需要将它也转换为 SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = _mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // 更新 VulkanImage 对象内部的布局状态，现在整个图像都处于正确的布局
    _layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// --- 内部辅助函数 ---
void VulkanImage::createImageView(VkImageAspectFlags aspectFlags, VkImageViewType viewType) {
    _view = _context->createImageView(_image, _format, aspectFlags, _mipLevels, viewType);
}

void VulkanImage::createSampler(VkSamplerAddressMode addressMode) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_TRUE;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(_context->getPhysicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(_mipLevels);
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(_context->getDevice(), &samplerInfo, nullptr, &_sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

VkDescriptorImageInfo VulkanImage::getDescriptorInfo() const {
    if (_sampler == VK_NULL_HANDLE || _view == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot get descriptor info from an image without a sampler or view!");
        throw std::runtime_error("Cannot get descriptor info from an image without a sampler or view!");
    }
    // The layout must be what the shader expects, usually SHADER_READ_ONLY_OPTIMAL
    return { _sampler, _view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
}

VkDescriptorImageInfo VulkanImage::getDescriptorInfoForStorage() const {
    if (_view == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot get storage descriptor info from an image without a view!");
    }
    // 当作为 STORAGE_IMAGE 绑定时，布局必须是 GENERAL
    // 并且 sampler 必须是 VK_NULL_HANDLE
    return { VK_NULL_HANDLE, _view, VK_IMAGE_LAYOUT_GENERAL };
}

std::unique_ptr<VulkanImage> VulkanImage::createImage2D(VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
    auto image = std::make_unique<VulkanImage>(
        context, extent, format, usage, properties,
        1, VK_SAMPLE_COUNT_1_BIT, 0, 1
    );
    image->createImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
    image->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    return image;
}
