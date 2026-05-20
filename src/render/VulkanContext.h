#pragma once
#include <volk.h>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

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
    // std::optional<uint32_t> transferFamily; // 可以为未来做准备

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct WindowInfo {
    int width;
    int height;
    std::string title;
};

class VulkanSwapchain;

class VulkanContext {
public:
    VulkanContext(WindowInfo& info);
    ~VulkanContext();

    // 禁止拷贝
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // --- 核心对象 Getters ---
    VkInstance getInstance() const { return _instance; }
    VkPhysicalDevice getPhysicalDevice() const { return _physicalDevice; }
    VkDevice getDevice() const { return _device; }
    VkSurfaceKHR getSurface() const { return _surface; }
    GLFWwindow* getWindow() const { return _window; }
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
        VkImageCreateFlags flags,       // <-- 接收新参数
        uint32_t arrayLayers          // <-- 接收新参数
    ) const;

    VkImageView createImageView(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectFlags,
        uint32_t mipLevels,
        VkImageViewType viewType        // <-- 接收新参数
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

    // 用于一次性命令的辅助函数 (注意：需要调用者提供 Command Pool 和 Queue)
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

    bool framebufferResized = false;
private:
    void initWindow();
    void initVulkan();

    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();

    // 内部查询和检查函数
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkSampleCountFlagBits getMaxUsableSampleCount();

    // 窗口回调
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    
    // 调试回调
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    WindowInfo& _info;
    GLFWwindow* _window = nullptr;

    VkInstance _instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;

    QueueFamilyIndices _queueFamilyIndices;
    VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
};
