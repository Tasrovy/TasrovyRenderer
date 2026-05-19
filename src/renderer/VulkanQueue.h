#pragma once
#include <vulkan/vulkan.h>
#include "VulkanContext.h"

// 前向声明 VulkanContext，避免循环依赖
class VulkanContext;

enum class QueueType {
    Graphics,
    Present,
    Compute,
    Transfer // 还可以为专门的传输队列添加类型
};

class VulkanQueue {
public:
    // 构造函数现在接收 Context 和类型
    VulkanQueue(VulkanContext& context, QueueType type);

    VkQueue getQueue() const { return _queue; }
    uint32_t getFamilyIndex() const { return _familyIndex; }

private:
    VkQueue _queue = VK_NULL_HANDLE;
    uint32_t _familyIndex;
};