#pragma once

#include "CommandList.h"

namespace Tasrovy::RHI {

class ICommandListBackend {
public:
    virtual ~ICommandListBackend() = default;
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void attachNativeCommandBuffer(uint64_t handle) = 0;
    virtual uint64_t nativeCommandBuffer() const = 0;
    virtual void beginRenderPass(Pass& pass) = 0;
    virtual void endRenderPass() = 0;
    virtual void beginSwapchainRenderPass(const SwapchainRenderTarget& target) = 0;
    virtual void endSwapchainRenderPass(const SwapchainRenderTarget& target) = 0;
    virtual void renderOverlay(
        RenderOverlay& overlay, const SwapchainRenderTarget& target,
        uint64_t frameToken) = 0;
    virtual void bindPipeline(Pipeline& pipeline, bool compute) = 0;
    virtual void bindVertexBuffer(Buffer& buffer) = 0;
    virtual void bindIndexBuffer(Buffer& buffer) = 0;
    virtual void bindDescriptorSet(
        uint32_t setIndex, const DescriptorSet& descriptorSet) = 0;
    virtual void setViewport(
        float x, float y, float width, float height,
        float minDepth, float maxDepth) = 0;
    virtual void setScissor(
        int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
    virtual void setVirtualShadowPage(
        const CommandList::VirtualShadowPageDesc& page) = 0;
    virtual void setFrontFace(FrontFace frontFace) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount) = 0;
    virtual void drawIndexed(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
        int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void drawIndexedIndirect(
        Buffer& indirectBuffer, uint64_t offset,
        uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void copyBuffer(Buffer& src, Buffer& dst, uint64_t size) = 0;
    virtual void pipelineBarrier(
        PipelineStage srcStage, PipelineStage dstStage) = 0;
    virtual void bufferMemoryBarrier(
        Buffer& buffer, PipelineStage srcStage, PipelineStage dstStage,
        ResourceAccess srcAccess, ResourceAccess dstAccess) = 0;
    virtual void transitionImage(
        Image& image, ImageLayout oldLayout, ImageLayout newLayout,
        uint32_t aspectMask) = 0;
    virtual void resetTimestampQueryPool(
        uint64_t queryPool, uint32_t queryCount) = 0;
    virtual void writeTimestamp(
        uint64_t queryPool, uint32_t queryIndex, bool begin) = 0;
};

} // namespace Tasrovy::RHI
