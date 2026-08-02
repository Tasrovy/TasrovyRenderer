#ifndef VULKAN_IMAGE_H
#define VULKAN_IMAGE_H

#include "VulkanContext.h"
#include <string>
#include <vector>
#include <memory>
#include <array>
#include <cstddef>

// 前向声明，避免循环包含
class ImmediateSubmitter;
class VulkanBuffer;

class VulkanImage {
public:
    // --- 静态工厂函数 ---

    // 创建标准 2D 纹理，可选择是否生成 Mipmaps
    static std::unique_ptr<VulkanImage> createTexture(
        VulkanContext& context,
        ImmediateSubmitter& uploader,
        const void* pixels,
        size_t pixelBytes,
        uint32_t width,
        uint32_t height,
        bool generateMipmaps,
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB
    );

    static std::unique_ptr<VulkanImage> createSolidTexture(
        VulkanContext& context,
        ImmediateSubmitter& uploader,
        const std::array<float, 4>& color,
        VkFormat format
    );

    static std::unique_ptr<VulkanImage> createImage2D(
        VulkanContext& context,
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    // 创建立方体贴图 (天空盒)
    static std::unique_ptr<VulkanImage> createCubemap(
        VulkanContext& context,
        ImmediateSubmitter& uploader,
        const void* pixels,
        size_t faceBytes,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB
    );

    // 创建渲染附件 (颜色/深度)
    static std::unique_ptr<VulkanImage> createAttachment(
        VulkanContext& context,
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
    );

    static std::unique_ptr<VulkanImage> createVirtualShadowAtlas(
        VulkanContext& context,
        VkExtent2D extent,
        VkFormat format
    );

    static std::unique_ptr<VulkanImage> createCube(
        VulkanContext& context,
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        uint32_t mipLevels = 1
    );

    ~VulkanImage();

    // 禁止拷贝
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;


    // --- 成员函数 ---

    // 在给定的命令缓冲区中记录一个布局转换命令
    void recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout);

    // --- Getters ---
    VkImage getImage() const { return _image; }
    VkImageView getView() const { return _view; }
    VkSampler getSampler() const { return _sampler; }
    VkFormat getFormat() const { return _format; }
    VkExtent2D getExtent() const { return _extent; }
    VkImageLayout getLayout() const { return _layout; }
    uint32_t getMipLevels() const { return _mipLevels; }
    VkSampleCountFlagBits getSampleCount() const { return _msaaCount; }

    // 获取用于更新描述符集的信息
    VkDescriptorImageInfo getDescriptorInfo() const;
    VkDescriptorImageInfo getDescriptorInfoForStorage() const;

    // 统一的私有构造函数
    VulkanImage(
        VulkanContext& context,
        VkExtent2D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        uint32_t mipLevels,
        VkSampleCountFlagBits numSamples,
        VkImageCreateFlags createFlags,
        uint32_t arrayLayers
    );

    void createImageView(VkImageAspectFlags aspectFlags, VkImageViewType viewType);
private:

    // 内部辅助函数
    void createSampler(
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    void recordGenerateMipmaps(VkCommandBuffer cmd);

    VulkanContext* _context; // 指针，因为 Image 可能被移动
    VkImage _image = VK_NULL_HANDLE;
    VkDeviceMemory _memory = VK_NULL_HANDLE;
    VkDeviceSize _allocationSize = 0;
    VkImageView _view = VK_NULL_HANDLE;
    VkSampler _sampler = VK_NULL_HANDLE;

    VkFormat _format;
    VkExtent2D _extent;
    VkImageLayout _layout;
    uint32_t _mipLevels;
    VkSampleCountFlagBits _msaaCount;
    VkImageCreateFlags _imageCreateFlags;
};

#endif // VULKAN_IMAGE_H
