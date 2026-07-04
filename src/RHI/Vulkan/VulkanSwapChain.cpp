#include "VulkanSwapchain.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <limits>
#include "Dependencies.h"
#include <Logger.hpp>

VulkanSwapchain::VulkanSwapchain(VulkanContext& context) 
    : _context(context){
    init();
    _colorAttachment = VulkanImage::createAttachment(_context, _extent, _imageFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, _context.getMsaaSamples());
    _depthAttachment = VulkanImage::createAttachment(_context, _extent, _context.findDepthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, _context.getMsaaSamples());
}

VulkanSwapchain::~VulkanSwapchain() {
    cleanup();
}

void VulkanSwapchain::init() {
    SwapChainSupportDetails details = _context.querySwapChainSupport();

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(details.presentModes);
    VkExtent2D extent = chooseSwapExtent2D(details.capabilities, _context.getFramebufferWidth(), _context.getFramebufferHeight());

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }
    LOG_INFO("Swapchain Image Count: {}", imageCount);
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = _context.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices queueFamilyIndices = _context.getQueueFamilyIndices();
    std::array<uint32_t, 2> queueFamilyIndicesArray = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };
    if (queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesArray.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(_context.getDevice(), &createInfo, nullptr, &_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    _imageFormat = surfaceFormat.format;
    _extent = extent;

    // 获取交换链图像
    uint32_t count;
    vkGetSwapchainImagesKHR(_context.getDevice(), _swapchain, &count, nullptr);
    _images.resize(count);
    vkGetSwapchainImagesKHR(_context.getDevice(), _swapchain, &count, _images.data());
	_imageLayouts.resize(_images.size(), VK_IMAGE_LAYOUT_UNDEFINED);

    // 创建图像视图
    _imageViews.resize(_images.size());
    for (size_t i = 0; i < _images.size(); i++) {
        _imageViews[i] = _context.createImageView(_images[i], _imageFormat, VK_IMAGE_ASPECT_COLOR_BIT,1,VK_IMAGE_VIEW_TYPE_2D);
    }
    
}

void VulkanSwapchain::cleanup() {
    for (auto imageView : _imageViews) {
        vkDestroyImageView(_context.getDevice(), imageView, nullptr);
    }
    vkDestroySwapchainKHR(_context.getDevice(), _swapchain, nullptr);
    _colorAttachment.reset();
	_depthAttachment.reset();
}

void VulkanSwapchain::recreate(VkExtent2D newExtent) {
    vkDeviceWaitIdle(_context.getDevice());
    cleanup();
    _extent = newExtent;
    init();
}

VkResult VulkanSwapchain::acquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(_context.getDevice(), _swapchain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphore, VK_NULL_HANDLE, imageIndex);
}

VkResult VulkanSwapchain::present(VkQueue presentQueue, VkSemaphore waitSemaphore, uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    
    VkSwapchainKHR swapchains[] = {_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

void VulkanSwapchain::recordLayoutTransition(VkCommandBuffer cmd, uint32_t imageIndex, VkImageLayout newLayout) {
    VkImageLayout oldLayout = _imageLayouts[imageIndex];

    if (oldLayout == newLayout) {
        return; // 布局已是目标布局，无需转换
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _images[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkAccessFlags sourceAccessMask;
    VkPipelineStageFlags destinationStage;
    VkAccessFlags destinationAccessMask;

    // 定义转换路径
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        sourceAccessMask = 0;
        destinationAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        sourceAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        destinationAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        sourceAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        destinationAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else {
        throw std::invalid_argument("unsupported layout transition in VulkanSwapchain!");
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 更新内部状态，记录新的布局
    _imageLayouts[imageIndex] = newLayout;
}

void VulkanSwapchain::recreate() {
	cleanup();
	init();
    _colorAttachment = VulkanImage::createAttachment(_context, _extent, _imageFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, _context.getMsaaSamples());
    _depthAttachment = VulkanImage::createAttachment(_context, _extent, _context.findDepthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, _context.getMsaaSamples());
}