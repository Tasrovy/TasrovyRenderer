#pragma once

#include <memory>
#include <cstdint>

namespace Tasrovy::RHI {

class Buffer;
class Pass;
class DescriptorSet;
class Image;

enum class ImageLayout {
    Undefined,
    ColorAttachment,
    DepthAttachment,
    DepthReadOnly,
    ShaderRead,
    Present
};

class CommandList : public std::enable_shared_from_this<CommandList> {
public:
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
    void setFrontFace(uint32_t frontFace);

    // --- Draw ---
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1);
    void drawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount = 1,
        uint32_t firstIndex = 0,
        int32_t vertexOffset = 0,
        uint32_t firstInstance = 0);

    // --- Copy ---
    void copyBuffer(Buffer& src, Buffer& dst, uint64_t size);

    // --- Barrier ---
    void pipelineBarrier(uint32_t srcStage, uint32_t dstStage);
    void transitionImage(Image& image, ImageLayout oldLayout, ImageLayout newLayout, uint32_t aspectMask = 0);

private:
    CommandList() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
