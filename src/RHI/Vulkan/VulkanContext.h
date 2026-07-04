#pragma once
#include <volk.h>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <functional>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    SwapChainSupportDetails() {
        capabilities = {};
    }
};

// Queue Family 结构体
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanSwapchain;

class VulkanContext {
public:
    using SurfaceCreator = std::function<VkSurfaceKHR(VkInstance)>;

    VulkanContext(const char* appName,
                  const std::vector<const char*>& instanceExtensions,
                  SurfaceCreator surfaceCreator,
                  int fbWidth, int fbHeight);
    ~VulkanContext();

    // 禁止拷贝
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // --- 核心对象 Getters ---
    VkInstance getInstance() const { return _instance; }
    VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
    VkDevice getDevice() const { return _device; }
    VkSurfaceKHR getSurface() const { return _surface; }
    const QueueFamilyIndices& getQueueFamilyIndices() const { return _queueFamilyIndices; }
    VkSampleCountFlagBits getMsaaSamples() const { return _msaaSamples; }

    // --- 底层辅助函数 ---
    std::vector<char> readFile(const std::string& filename) const;
    VkShaderModule createShaderModule(const std::vector<char>& code) const;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    // 用于资源创建的辅助函数
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) const;
    void createImage(
        uint32_t width, uint32_t height,
        uint32_t mipLevels, VkSampleCountFlagBits numSamples,
        VkFormat format, VkImageTiling tiling,
        VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
        VkImage& image, VkDeviceMemory& imageMemory,
        VkImageCreateFlags flags,
        uint32_t arrayLayers
    ) const;

    VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectFlags,
        uint32_t mipLevels,
        VkImageViewType viewType
    ) const;

    VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectFlags,
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
        uint32_t layerCount = 1,
        uint32_t baseArrayLayer = 0,
        uint32_t levelCount = 1,
        uint32_t baseMipLevel = 0
    ) const;

    // 用于一次性命令的辅助函数
    VkCommandBuffer beginSingleTimeCommands(VkCommandPool commandPool) const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool commandPool) const;

    // --- 资源操作辅助函数 ---
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkQueue queue, VkCommandPool commandPool) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, VkQueue queue, VkCommandPool commandPool) const;

    // --- 查询函数 ---
    SwapChainSupportDetails querySwapChainSupport() const;
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    VkFormat findDepthFormat() const;
    void CheckFormatChange(VulkanSwapchain& swapchain);

    int getFramebufferWidth() const { return _fbWidth; }
    int getFramebufferHeight() const { return _fbHeight; }
    void updateFramebufferSize(int w, int h) { _fbWidth = w; _fbHeight = h; }

    bool framebufferResized = false;
private:
    void createInstance(const std::vector<const char*>& instanceExtensions);
    void setupDebugMessenger();
    void createSurface(VkInstance instance);
    void pickPhysicalDevice();
    void createLogicalDevice();

    // 内部查询和检查函数
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkSampleCountFlagBits getMaxUsableSampleCount();

    // 调试回调
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    std::string _appName;
    SurfaceCreator _surfaceCreator;
    int _fbWidth = 0;
    int _fbHeight = 0;

    VkInstance _instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;

    QueueFamilyIndices _queueFamilyIndices;
    VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
};
