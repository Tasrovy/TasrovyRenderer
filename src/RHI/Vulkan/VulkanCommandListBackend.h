#pragma once

#include "../CommandListBackend.h"

#include <volk.h>

class VulkanContext;

namespace Tasrovy::RHI::Vulkan {

class VulkanCommandListBackend final : public ICommandListBackend {
public:
    explicit VulkanCommandListBackend(VulkanContext& context);
    ~VulkanCommandListBackend() override;

    void begin() override;
    void end() override;
    void attachNativeCommandBuffer(uint64_t handle) override;
    uint64_t nativeCommandBuffer() const override;
    void beginRenderPass(Pass& pass) override;
    void endRenderPass() override;
    void beginSwapchainRenderPass(const SwapchainRenderTarget& target) override;
    void endSwapchainRenderPass(const SwapchainRenderTarget& target) override;
    void renderOverlay(
        RenderOverlay& overlay, const SwapchainRenderTarget& target,
        uint64_t frameToken) override;
    void bindPipeline(Pipeline& pipeline, bool compute) override;
    void bindVertexBuffer(Buffer& buffer) override;
    void bindIndexBuffer(Buffer& buffer) override;
    void bindDescriptorSet(
        uint32_t setIndex, const DescriptorSet& descriptorSet) override;
    void setViewport(
        float x, float y, float width, float height,
        float minDepth, float maxDepth) override;
    void setScissor(
        int32_t x, int32_t y, uint32_t width, uint32_t height) override;
    void setVirtualShadowPage(
        const CommandList::VirtualShadowPageDesc& page) override;
    void setFrontFace(FrontFace frontFace) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount) override;
    void drawIndexed(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
        int32_t vertexOffset, uint32_t firstInstance) override;
    void drawIndexedIndirect(
        Buffer& indirectBuffer, uint64_t offset,
        uint32_t drawCount, uint32_t stride) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void copyBuffer(Buffer& src, Buffer& dst, uint64_t size) override;
    void pipelineBarrier(
        PipelineStage srcStage, PipelineStage dstStage) override;
    void bufferMemoryBarrier(
        Buffer& buffer, PipelineStage srcStage, PipelineStage dstStage,
        ResourceAccess srcAccess, ResourceAccess dstAccess) override;
    void transitionImage(
        Image& image, ImageLayout oldLayout, ImageLayout newLayout,
        uint32_t aspectMask) override;
    void resetTimestampQueryPool(
        uint64_t queryPool, uint32_t queryCount) override;
    void writeTimestamp(
        uint64_t queryPool, uint32_t queryIndex, bool begin) override;

private:
    VulkanContext& context_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkPipelineLayout boundPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineBindPoint boundPipelineBindPoint_ =
        VK_PIPELINE_BIND_POINT_GRAPHICS;
    bool recording_ = false;
};

} // namespace Tasrovy::RHI::Vulkan
