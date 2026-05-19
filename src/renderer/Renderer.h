#pragma once
#include <vulkan/vulkan.h>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanBuffer.h"
#include "VulkanQueue.h"
#include "VulkanImage.h"
#include "Dependencies.h"

class Renderer {
public:
    Renderer(VulkanContext& context, uint32_t maxFramesInFlight);
    ~Renderer();

    // 禁止拷贝
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // 开始一帧的渲染，如果成功，返回一个可以记录命令的 Command Buffer
    // 如果返回 nullptr，意味着交换链需要重建，应该跳过这一帧
    VkCommandBuffer beginFrame(VulkanSwapchain& swapchain);
    void beginRenderPass(VkCommandBuffer cmd, VulkanSwapchain& swapchain);
    void endRenderPass(VkCommandBuffer cmd, VulkanSwapchain& swapchain);
    
    // 结束一帧的渲染并提交
    void endFrame(VulkanSwapchain& swapchain,VulkanQueue graphicsQueue,VulkanQueue presentQueue);
	uint32_t getMaxFramesInFlight() const { return _maxFramesInFlight; }
    uint32_t getCurrentFrame() const { return _currentFrame; }
	uint32_t getImageIndex() const { return _imageIndex; }
    void draw(VulkanSwapchain& swapchain, std::unique_ptr<VulkanPipeline>& graphicsPipeline, 
        VulkanBuffer& indexBuffer, VulkanBuffer& vertexBuffer, 
        std::vector<VkDescriptorSet> descriptorSets, uint32_t size, 
        VulkanQueue graphicsQueue, VulkanQueue presentQueue);
	//void updateuniformBuffer(VulkanBuffer& uniformBuffer, const UniformBufferObject& ubo);
private:
    VulkanContext& _context;
    uint32_t _maxFramesInFlight;
    uint32_t _currentFrame = 0;
    uint32_t _imageIndex = 0;

    VkCommandPool _commandPool;
    std::vector<VkCommandBuffer> _commandBuffers;
    std::vector<VkSemaphore> _imageAvailableSemaphores;
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    std::vector<VkFence> _inFlightFences;
};