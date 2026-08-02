#pragma once

#include <memory>
#include <cstdint>

namespace Tasrovy::RHI {

class Buffer;
class Pass;
class DescriptorSet;
class Image;
class RenderOverlay;

struct SwapchainRenderTarget {
    uint64_t colorImage = 0;
    uint64_t colorView = 0;
    uint64_t resolveImage = 0;
    uint64_t resolveView = 0;
    uint64_t depthImage = 0;
    uint64_t depthView = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool resolveImageWasPresented = false;
};

enum class ImageLayout {
    Undefined,
    ColorAttachment,
    DepthAttachment,
    DepthReadOnly,
    ShaderRead,
    General,
    Present
};

enum class PipelineStage : uint32_t {
    None = 0,
    TopOfPipe = 1u << 0u,
    Host = 1u << 1u,
    DrawIndirect = 1u << 2u,
    VertexShader = 1u << 3u,
    FragmentShader = 1u << 4u,
    ComputeShader = 1u << 5u,
    Transfer = 1u << 6u,
    ColorAttachmentOutput = 1u << 7u,
    DepthStencilTests = 1u << 8u,
    BottomOfPipe = 1u << 9u,
    AllCommands = 1u << 10u
};

constexpr PipelineStage operator|(PipelineStage lhs, PipelineStage rhs) {
    return static_cast<PipelineStage>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

enum class ResourceAccess : uint32_t {
    None = 0,
    HostWrite = 1u << 0u,
    IndirectCommandRead = 1u << 1u,
    ShaderRead = 1u << 2u,
    ShaderWrite = 1u << 3u,
    TransferRead = 1u << 4u,
    TransferWrite = 1u << 5u,
    ColorAttachmentRead = 1u << 6u,
    ColorAttachmentWrite = 1u << 7u,
    DepthStencilRead = 1u << 8u,
    DepthStencilWrite = 1u << 9u,
    MemoryRead = 1u << 10u,
    MemoryWrite = 1u << 11u
};

constexpr ResourceAccess operator|(ResourceAccess lhs, ResourceAccess rhs) {
    return static_cast<ResourceAccess>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

class CommandList : public std::enable_shared_from_this<CommandList> {
public:
    struct VirtualShadowPageDesc {
        uint32_t pageX = 0;
        uint32_t pageY = 0;
        uint32_t pageSize = 0;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
    };

    static std::shared_ptr<CommandList> create();
    ~CommandList();

    void setBackendContext(void* nativeContext);
    void useNativeCommandBuffer(uint64_t nativeCommandBuffer);

    // --- Lifecycle ---
    void begin();
    void end();
    uint64_t getNativeCommandBuffer() const;

    // --- Render pass ---
    void beginRenderPass(Pass& pass);
    void beginRenderPass(Pass& pass,
        uint64_t colorView, uint64_t resolveView, uint64_t depthView,
        uint32_t width, uint32_t height);
    void endRenderPass();
    void beginSwapchainRenderPass(const SwapchainRenderTarget& target);
    void endSwapchainRenderPass(const SwapchainRenderTarget& target);
    void renderOverlay(
        RenderOverlay& overlay,
        const SwapchainRenderTarget& target);

    // --- Pipeline ---
    void bindPipeline(uint64_t nativePipeline, uint64_t nativeLayout, uint32_t bindPoint = 0);

    // --- Buffers ---
    void bindVertexBuffer(Buffer& buffer);
    void bindIndexBuffer(Buffer& buffer);

    // --- Descriptors ---
    void setPipelineLayout(uint64_t nativeLayout);
    void bindDescriptorSet(uint32_t setIndex, void* descriptorSet);
    void bindDescriptorSet(uint32_t setIndex, const DescriptorSet& descriptorSet);

    // --- State ---
    void setViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    // Maps a virtual shadow page to a rectangular region of a physical depth
    // atlas. Backends implement this without exposing native viewport types.
    void setVirtualShadowPage(const VirtualShadowPageDesc& page);
    void setFrontFace(uint32_t frontFace);

    // --- Draw ---
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1);
    void drawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount = 1,
        uint32_t firstIndex = 0,
        int32_t vertexOffset = 0,
        uint32_t firstInstance = 0);
    void drawIndexedIndirect(
        Buffer& indirectBuffer,
        uint64_t offset,
        uint32_t drawCount,
        uint32_t stride);
    void dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);

    // --- Copy ---
    void copyBuffer(Buffer& src, Buffer& dst, uint64_t size);

    // --- Barrier ---
    void pipelineBarrier(PipelineStage srcStage, PipelineStage dstStage);
    void bufferMemoryBarrier(
        Buffer& buffer,
        PipelineStage srcStage,
        PipelineStage dstStage,
        ResourceAccess srcAccess,
        ResourceAccess dstAccess);
    void transitionImage(Image& image, ImageLayout oldLayout, ImageLayout newLayout, uint32_t aspectMask = 0);

    // --- Queries ---
    void resetTimestampQueryPool(uint64_t nativeQueryPool, uint32_t queryCount);
    void writeTimestamp(uint64_t nativeQueryPool, uint32_t queryIndex, bool begin);

private:
    CommandList() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
