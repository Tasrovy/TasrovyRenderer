#include <stdexcept>
#include "VulkanQueue.h"
#include "VulkanContext.h"
VulkanQueue::VulkanQueue(VulkanContext& context, QueueType type) {
    const QueueFamilyIndices& indices = context.getQueueFamilyIndices();
    VkDevice device = context.getDevice();

    switch (type) {
        case QueueType::Graphics:
            if (!indices.graphicsFamily.has_value()) {
                throw std::runtime_error("Graphics queue family not found!");
            }
            _familyIndex = indices.graphicsFamily.value();
            break;

        case QueueType::Present:
            if (!indices.presentFamily.has_value()) {
                throw std::runtime_error("Present queue family not found!");
            }
            _familyIndex = indices.presentFamily.value();
            break;

        case QueueType::Compute:
            if (!indices.computeFamily.has_value()) {
                // 如果没有专门的计算队列，通常可以回退到图形队列
                if (!indices.graphicsFamily.has_value()) {
                   throw std::runtime_error("Compute/Graphics queue family not found!");
                }
                _familyIndex = indices.graphicsFamily.value();
            } else {
                _familyIndex = indices.computeFamily.value();
            }
            break;

        // 可以在这里添加 Transfer 类型的处理
        // case QueueType::Transfer: ...

        default:
            throw std::runtime_error("Unknown queue type requested!");
    }
    
    // 从设备获取实际的队列句柄
    vkGetDeviceQueue(device, _familyIndex, 0, &_queue);
}