#include "ImmediateSubmitter.h"
#include <stdexcept>

// 辅助函数，确定布局转换的阶段和访问掩码
// 您可以根据需要扩展这个函数以支持更多的转换类型
static void getPipelineStageAndAccessMasks(
    VkImageLayout oldLayout, 
    VkImageLayout newLayout, 
    VkPipelineStageFlags& sourceStage, 
    VkAccessFlags& sourceAccessMask, 
    VkPipelineStageFlags& destinationStage, 
    VkAccessFlags& destinationAccessMask) 
{
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        sourceAccessMask = 0;
        destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        sourceAccessMask = 0;
        destinationAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        // 默认情况或未支持的转换
        sourceAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        destinationAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}


ImmediateSubmitter::ImmediateSubmitter(VulkanContext& context, VulkanQueue& queue)
    : _context(context), _queue(queue) {
    
    // 1. 创建一个专用于一次性命令的命令池
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queue.getFamilyIndex();
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 提示驱动这个池的命令缓冲区是短暂的，可能带来性能优化
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; 
    
    if (vkCreateCommandPool(_context.getDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create immediate submit command pool!");
    }

    // 2. 创建一个用于同步的 Fence，初始状态为已触发，以便第一次等待可以立即通过
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(_context.getDevice(), &fenceInfo, nullptr, &_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create immediate submit fence!");
    }
    // 立即重置，使其进入未触发状态
    vkResetFences(_context.getDevice(), 1, &_fence);
}

ImmediateSubmitter::~ImmediateSubmitter() {
    vkDestroyFence(_context.getDevice(), _fence, nullptr);
    vkDestroyCommandPool(_context.getDevice(), _commandPool, nullptr);
}

void ImmediateSubmitter::submit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VkDevice device = _context.getDevice();
    VkQueue queue = _queue.getQueue();

    // 1. 分配命令缓冲区
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate immediate command buffer!");
    }

    // 2. 开始记录命令
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // 3. 执行调用者提供的、用于记录具体命令的函数
    function(cmd);

    // 4. 结束记录
    vkEndCommandBuffer(cmd);

    // 5. 将命令提交到队列
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // 使用 fence 来等待执行完成
    // 首先重置 fence 为未触发状态
    vkResetFences(device, 1, &_fence);
    
    if (vkQueueSubmit(queue, 1, &submitInfo, _fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit immediate command buffer!");
    }

    // 6. 阻塞CPU，直到GPU完成命令
    vkWaitForFences(device, 1, &_fence, VK_TRUE, UINT64_MAX);

    // 7. 释放临时的命令缓冲区
    vkFreeCommandBuffers(device, _commandPool, 1, &cmd);
}

// --- 高级便利函数的实现 ---

void ImmediateSubmitter::copyBuffer(VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size) {
    submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, src.getBuffer(), dst.getBuffer(), 1, &copyRegion);
    });
}

void ImmediateSubmitter::copyDataToBuffer(void* src, VulkanBuffer& dst, VkDeviceSize size) {
    // 1. 创建 staging buffer
    VulkanBuffer staging(
        _context,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    staging.setData(src, size);
    // 3. 把 staging buffer 的内容复制到真正的 buffer
    submit([&](VkCommandBuffer cmd) {        
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd,staging.getBuffer(),dst.getBuffer(),1,&copyRegion);
    });
}

void ImmediateSubmitter::transitionImageLayout(VulkanImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, VkImageAspectFlags aspectMask) {
    submit([&image, format, oldLayout, newLayout, mipLevels, aspectMask](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.getImage();
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkAccessFlags sourceAccessMask;
        VkPipelineStageFlags destinationStage;
        VkAccessFlags destinationAccessMask;
        
        getPipelineStageAndAccessMasks(oldLayout, newLayout, sourceStage, sourceAccessMask, destinationStage, destinationAccessMask);
        
        barrier.srcAccessMask = sourceAccessMask;
        barrier.dstAccessMask = destinationAccessMask;

        vkCmdPipelineBarrier(cmd,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    });
}

void ImmediateSubmitter::copyBufferToImage(VulkanBuffer& buffer, VulkanImage& image, uint32_t width, uint32_t height) {
    submit([&image, &buffer, width, height](VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(cmd, buffer.getBuffer(), image.getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });
}

