#include "ImmediateSubmitter.h"
#include <stdexcept>

// 杈呭姪鍑芥暟锛岀‘瀹氬竷灞€杞崲鐨勯樁娈靛拰璁块棶鎺╃爜
// 鎮ㄥ彲浠ユ牴鎹渶瑕佹墿灞曡繖涓嚱鏁颁互鏀寔鏇村鐨勮浆鎹㈢被鍨?
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
        // 榛樿鎯呭喌鎴栨湭鏀寔鐨勮浆鎹?
        sourceAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        destinationAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}


ImmediateSubmitter::ImmediateSubmitter(VulkanContext& context, VulkanQueue& queue)
    : _context(context), _queue(queue) {
    
    // 1. 鍒涘缓涓€涓笓鐢ㄤ簬涓€娆℃€у懡浠ょ殑鍛戒护姹?
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queue.getFamilyIndex();
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT 鎻愮ず椹卞姩杩欎釜姹犵殑鍛戒护缂撳啿鍖烘槸鐭殏鐨勶紝鍙兘甯︽潵鎬ц兘浼樺寲
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; 
    
    if (vkCreateCommandPool(_context.getDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create immediate submit command pool!");
    }

    // 2. 鍒涘缓涓€涓敤浜庡悓姝ョ殑 Fence锛屽垵濮嬬姸鎬佷负宸茶Е鍙戯紝浠ヤ究绗竴娆＄瓑寰呭彲浠ョ珛鍗抽€氳繃
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(_context.getDevice(), &fenceInfo, nullptr, &_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create immediate submit fence!");
    }
    // 绔嬪嵆閲嶇疆锛屼娇鍏惰繘鍏ユ湭瑙﹀彂鐘舵€?
}

ImmediateSubmitter::~ImmediateSubmitter() {
    vkWaitForFences(_context.getDevice(), 1, &_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(_context.getDevice(), _fence, nullptr);
    vkDestroyCommandPool(_context.getDevice(), _commandPool, nullptr);
}

void ImmediateSubmitter::submit(std::function<void(VkCommandBuffer cmd)>&& function) {
    std::lock_guard<std::mutex> lock(_submitMutex);
    VkDevice device = _context.getDevice();
    VkQueue queue = _queue.getQueue();

    if (vkWaitForFences(device, 1, &_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("Failed to wait for immediate submit fence!");
    }
    if (vkResetFences(device, 1, &_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset immediate submit fence!");
    }

    // 1. 鍒嗛厤鍛戒护缂撳啿鍖?
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate immediate command buffer!");
    }

    // 2. 寮€濮嬭褰曞懡浠?
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, _commandPool, 1, &cmd);
        throw std::runtime_error("Failed to begin immediate command buffer!");
    }

    // 3. 鎵ц璋冪敤鑰呮彁渚涚殑銆佺敤浜庤褰曞叿浣撳懡浠ょ殑鍑芥暟
    function(cmd);

    // 4. 缁撴潫璁板綍
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, _commandPool, 1, &cmd);
        throw std::runtime_error("Failed to end immediate command buffer!");
    }

    // 5. 灏嗗懡浠ゆ彁浜ゅ埌闃熷垪
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    // 浣跨敤 fence 鏉ョ瓑寰呮墽琛屽畬鎴?
    // 棣栧厛閲嶇疆 fence 涓烘湭瑙﹀彂鐘舵€?
    {
        // Renderer and upload work can reach the same VkQueue from different
        // threads. Vulkan requires all host access to a queue to be serialized.
        std::scoped_lock queueLock(_context.getQueueMutex());
        if (vkQueueSubmit(queue, 1, &submitInfo, _fence) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, _commandPool, 1, &cmd);
            throw std::runtime_error("Failed to submit immediate command buffer!");
        }
    }

    // 6. 闃诲CPU锛岀洿鍒癎PU瀹屾垚鍛戒护
    if (vkWaitForFences(device, 1, &_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("Failed to wait for immediate command buffer!");
    }

    // 7. 閲婃斁涓存椂鐨勫懡浠ょ紦鍐插尯
    vkFreeCommandBuffers(device, _commandPool, 1, &cmd);
}

// --- 楂樼骇渚垮埄鍑芥暟鐨勫疄鐜?---

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
    // 1. 鍒涘缓 staging buffer
    VulkanBuffer staging(
        _context,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    staging.setData(src, size);
    // 3. 鎶?staging buffer 鐨勫唴瀹瑰鍒跺埌鐪熸鐨?buffer
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

