#pragma once
#include <volk.h>
#include <functional>

struct GLFWwindow;

class UIOverlay {
public:
    struct CreateInfo {
        GLFWwindow* window = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t minImageCount = 3;
        uint32_t imageCount = 3;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    };

    UIOverlay(const CreateInfo& info);
    ~UIOverlay();

    UIOverlay(const UIOverlay&) = delete;
    UIOverlay& operator=(const UIOverlay&) = delete;

    using DrawCallback = std::function<void()>;
    void setDrawCallback(DrawCallback cb) { _drawCallback = std::move(cb); }

    void beginFrame();
    void endFrame(VkCommandBuffer cmd, VkImageView colorView, VkExtent2D extent);

private:
    GLFWwindow* _window = nullptr;
    VkDevice _device = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkFormat _colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    DrawCallback _drawCallback;
};
