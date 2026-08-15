#pragma once
#include <volk.h>
#include "VulkanContext.h"
#include "VulkanSwapchain.h"
#include "VulkanBuffer.h"
#include "VulkanQueue.h"
#include "VulkanImage.h"

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
    
    // 结束一帧的渲染并提交
    void endFrame(VulkanSwapchain& swapchain, VulkanQueue& graphicsQueue, VulkanQueue& presentQueue);
    void abortFrame(VulkanQueue& graphicsQueue);
    void waitIdle();
	void onSwapchainRecreated(uint32_t imageCount);
	bool isSwapchainRebuildRequired() const { return _swapchainRebuildRequired; }
	void clearSwapchainRebuildRequest() { _swapchainRebuildRequired = false; }
	uint32_t getMaxFramesInFlight() const { return _maxFramesInFlight; }
    uint32_t getCurrentFrame() const { return _currentFrame; }
    uint32_t getImageIndex() const { return _imageIndex; }
    VkQueryPool getCurrentTimestampQueryPool() const;
    void setCurrentTimestampQueryCount(uint32_t queryCount);
    std::vector<double> consumeGpuTimestampDurations();
private:
    VulkanContext& _context;
    uint32_t _maxFramesInFlight;
    uint32_t _currentFrame = 0;
    uint32_t _imageIndex = 0;
    uint32_t _swapchainImageCount = 0;

    VkCommandPool _commandPool;
    std::vector<VkCommandBuffer> _commandBuffers;
    std::vector<VkSemaphore> _imageAvailableSemaphores;
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    std::vector<VkFence> _inFlightFences;
    std::vector<uint64_t> _frameSubmissionSerials;
    std::vector<VkQueryPool> _timestampQueryPools;
    std::vector<uint32_t> _timestampQueryCounts;
    float _timestampPeriodNanoseconds = 0.0f;
    static constexpr uint32_t MaxTimestampQueries = 256;
    bool _swapchainRebuildRequired = false;
    bool _frameOpen = false;
};
