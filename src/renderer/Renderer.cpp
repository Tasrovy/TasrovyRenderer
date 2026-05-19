#include "Renderer.h"
#include "VulkanContext.h"
#include <stdexcept>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

Renderer::Renderer(VulkanContext& context, uint32_t maxFramesInFlight)
    : _context(context), _maxFramesInFlight(maxFramesInFlight) {
    
    // 创建命令池
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _context.getQueueFamilyIndices().graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(_context.getDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
    
    // 创建命令缓冲区
    _commandBuffers.resize(_maxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = _maxFramesInFlight;
    if (vkAllocateCommandBuffers(_context.getDevice(), &allocInfo, _commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }

    // 创建同步对象
    _imageAvailableSemaphores.resize(_maxFramesInFlight);
    _renderFinishedSemaphores.resize(_maxFramesInFlight);
    _inFlightFences.resize(_maxFramesInFlight);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    for (uint32_t i = 0; i < _maxFramesInFlight; ++i) {
        vkCreateSemaphore(_context.getDevice(), &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]);
        vkCreateSemaphore(_context.getDevice(), &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]);
        vkCreateFence(_context.getDevice(), &fenceInfo, nullptr, &_inFlightFences[i]);
    }
}

Renderer::~Renderer() {
    for (uint32_t i = 0; i < _maxFramesInFlight; ++i) {
        vkDestroySemaphore(_context.getDevice(), _imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(_context.getDevice(), _renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(_context.getDevice(), _inFlightFences[i], nullptr);
    }
    vkDestroyCommandPool(_context.getDevice(), _commandPool, nullptr);
}

VkCommandBuffer Renderer::beginFrame(VulkanSwapchain& swapchain) {
    vkWaitForFences(_context.getDevice(), 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = swapchain.acquireNextImage(_imageAvailableSemaphores[_currentFrame], &_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // ... 通知上层进行重建 ...
        return nullptr;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(_context.getDevice(), 1, &_inFlightFences[_currentFrame]);
    
    VkCommandBuffer commandBuffer = _commandBuffers[_currentFrame];
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void Renderer::endFrame(VulkanSwapchain& swapchain,VulkanQueue graphicsQueue,VulkanQueue presentQueue) {
    VkCommandBuffer commandBuffer = _commandBuffers[_currentFrame];
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {_imageAvailableSemaphores[_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {_renderFinishedSemaphores[_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue.getQueue(), 1, &submitInfo, _inFlightFences[_currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkResult result = swapchain.present(presentQueue.getQueue(), _renderFinishedSemaphores[_currentFrame], _imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
       // ... 通知上层进行重建 ...
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
    
    _currentFrame = (_currentFrame + 1) % _maxFramesInFlight;
}

void Renderer::beginRenderPass(VkCommandBuffer cmd, VulkanSwapchain& swapchain) {
    // ASSUMPTION: _imageIndex 已经由 vkAcquireNextImageKHR 获得
    // 注意：交换链图像通常在 acquire 后 layout 是 PRESENT_SRC_KHR（而不是 UNDEFINED），
    // 所以最好使用 PRESENT_SRC_KHR -> COLOR_ATTACHMENT_OPTIMAL 的 barrier。
    // 这里示范使用 PRESENT_SRC_KHR；如果你的代码能保证是 UNDEFINED（例如首次创建），可改回 UNDEFINED。

    // ---- 1) 转换 swapchain image (resolve 目标) 到 COLOR_ATTACHMENT_OPTIMAL ----
    swapchain.recordLayoutTransition(
        cmd,
        _imageIndex,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	);

    // ---- 2) 转换 MSAA color image 到 COLOR_ATTACHMENT_OPTIMAL ----
    VkImageMemoryBarrier colorBarrier{};
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // 例如首次使用 MSAA 图像
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcAccessMask = 0;
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorBarrier.image = swapchain.getColorAttachmentImage();
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.baseMipLevel = 0;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.baseArrayLayer = 0;
    colorBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &colorBarrier
    );

    // ---- 3) 转换 depth/stencil 图像布局到 DEPTH_STENCIL_ATTACHMENT_OPTIMAL ----
    VkImageMemoryBarrier depthBarrier{};
    depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.srcAccessMask = 0;
    depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.image = swapchain.getDepthAttachmentImage();
    // 注意：如果 depth image 有 stencil，aspectMask 可设为 DEPTH | STENCIL
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depthBarrier.subresourceRange.baseMipLevel = 0;
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.baseArrayLayer = 0;
    depthBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &depthBarrier
    );

    // ---- 4) 配置动态渲染的附件描述 ----
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain.getColorAttachmentView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; // or SAMPLE_ZERO_BIT, etc.
    colorAttachment.resolveImageView = swapchain.getImageView(_imageIndex);
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = swapchain.getDepthAttachmentView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    // 如果你没有单独的 stencil view，不要传 pStencilAttachment（设为 nullptr）
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.flags = 0;
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = swapchain.getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = nullptr; // <-- 不要传未初始化或与 depth 相同的 view

    vkCmdBeginRendering(cmd, &renderingInfo);
}


void Renderer::endRenderPass(VkCommandBuffer cmd, VulkanSwapchain& swapchain) {
    // 1. 结束动态渲染
    vkCmdEndRendering(cmd);

    // 2. 图像布局转换：从 COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    //    这个转换是为了准备将图片呈现到屏幕上
    swapchain.recordLayoutTransition(
        cmd,
        _imageIndex,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	);
}

//void Renderer::updateuniformBuffer(VulkanBuffer& uniformBuffer, const UniformBufferObject& ubo) {
//
//}
void Renderer::draw(VulkanSwapchain& swapchain, std::unique_ptr<VulkanPipeline>& graphicsPipeline, VulkanBuffer& indexBuffer, VulkanBuffer& vertexBuffer, std::vector<VkDescriptorSet> descriptorSets, uint32_t size,VulkanQueue graphicsQueue, VulkanQueue presentQueue) {
    VkCommandBuffer commandBuffer = beginFrame(swapchain);

    beginRenderPass(commandBuffer, swapchain);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain.getExtent().width);
    viewport.height = static_cast<float>(swapchain.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchain.getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindIndexBuffer(commandBuffer, indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline->getLayout(), // 使用管线自己的布局
        0,                             // 绑定到 set 0
        1,                             // 绑定 1 个 set
        &descriptorSets[getCurrentFrame()], // 传入当前帧的 descriptor set
        0,
        nullptr
    );

    VkBuffer vertexBuffers[] = { vertexBuffer.getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdDrawIndexed(commandBuffer, size, 1, 0, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    endRenderPass(commandBuffer, swapchain);
    endFrame(swapchain, graphicsQueue, presentQueue);
}