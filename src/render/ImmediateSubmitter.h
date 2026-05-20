#pragma once
#include "VulkanContext.h"
#include "VulkanQueue.h"
#include <functional> // 用于 std::function
#include "VulkanImage.h"
#include "VulkanBuffer.h"

/*
 * @class ImmediateSubmitter
 * @brief 一个用于执行一次性、同步GPU命令的工具类。
 *
 * 这个类封装了一个命令池和一个围栏，提供了一个简单的接口来提交
 * 简短的命令（如资源拷贝、布局转换）并立即等待其完成。
 * 这对于资源初始化非常有用。
 * 注意：这是一个阻塞操作，不应该在性能敏感的主渲染循环中频繁使用。
 */
class ImmediateSubmitter {
public:
    // 构造函数需要知道使用哪个设备和队列来提交命令。
    ImmediateSubmitter(VulkanContext& context, VulkanQueue& queue);
    ~ImmediateSubmitter();

    // 禁止拷贝和赋值
    ImmediateSubmitter(const ImmediateSubmitter&) = delete;
    ImmediateSubmitter& operator=(const ImmediateSubmitter&) = delete;

    /*
     * @brief 提交一个包含一系列命令的函数对象（如lambda）。
     * @param function 一个接收 VkCommandBuffer 的函数，用于记录具体的GPU命令。
     */
    void submit(std::function<void(VkCommandBuffer cmd)>&& function);

    // --- 为了方便使用而提供的高级便利函数 ---
    
    void copyBuffer(VulkanBuffer &src, VulkanBuffer &dst, VkDeviceSize size);
    
    void copyDataToBuffer(void* src, VulkanBuffer& dst, VkDeviceSize size);

    void transitionImageLayout(VulkanImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
    
    void copyBufferToImage(VulkanBuffer& buffer, VulkanImage& image, uint32_t width, uint32_t height);
    
private:
    VulkanContext& _context;
    VulkanQueue& _queue;
    VkCommandPool _commandPool;
    VkFence _fence;
};