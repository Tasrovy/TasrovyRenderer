#pragma once
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <memory>
#include <iostream>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "VulkanContext.h"
#include "VulkanImage.h"
#include <limits>

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    if (!availableFormats.empty()) {
        return availableFormats[0];
    } else {
        throw std::runtime_error("No surface formats available!");
    }
}

// 选择最佳的呈现模式
static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            std::cout << "[INFO] Swapchain using immediate present mode." << std::endl;
            return availablePresentMode;
        }
    }

    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            std::cout << "[INFO] Swapchain using Mailbox present mode." << std::endl;
            return availablePresentMode;
        }
    }

    std::cout << "[INFO] Swapchain using FIFO present mode." << std::endl;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseSwapExtent2D(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } 
    else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}


class VulkanSwapchain {
public:
    VulkanSwapchain(VulkanContext& context);
    ~VulkanSwapchain();

    // 禁止拷贝
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void recreate(VkExtent2D newExtent);
    VkResult acquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t* imageIndex);
    VkResult present(VkQueue presentQueue, VkSemaphore waitSemaphore, uint32_t imageIndex);

    // Getters
    VkSwapchainKHR getSwapchainKHR() const { return _swapchain; }
    VkFormat getImageFormat() const { return _imageFormat; }
    VkExtent2D getExtent() const { return _extent; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(_images.size()); }
    VkImageView getImageView(int index) const { return _imageViews[index]; }
	VkImage getImage(int index) const { return _images[index]; }
	VkImageLayout getImageLayout(int index) const { return _imageLayouts[index]; }
    VkImageView getColorAttachmentView() const {
        if (_colorAttachment) {
            return _colorAttachment->getView();
        }
        return VK_NULL_HANDLE;
    }

    VkImageView getDepthAttachmentView() const {
        if (_depthAttachment) {
            return _depthAttachment->getView();
        }
        return VK_NULL_HANDLE;
    }
    VkImage getColorAttachmentImage() const {
        if (_colorAttachment) {
            return _colorAttachment->getImage();
        }
        return VK_NULL_HANDLE;
    }

    VkImage getDepthAttachmentImage() const {
        if (_depthAttachment) {
            return _depthAttachment->getImage();
        }
        return VK_NULL_HANDLE;
    }
    void recordLayoutTransition(VkCommandBuffer cmd, uint32_t imageIndex, VkImageLayout newLayout);
    void recreate();
private:
    void init();
    void cleanup();
    VulkanContext& _context;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    
    VkFormat _imageFormat;
    VkExtent2D _extent;
    
    std::vector<VkImage> _images;
    std::vector<VkImageView> _imageViews;
    std::vector<VkImageLayout> _imageLayouts;
    std::unique_ptr<VulkanImage> _colorAttachment;

    std::unique_ptr<VulkanImage> _depthAttachment;
};