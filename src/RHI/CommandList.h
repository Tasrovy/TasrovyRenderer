#pragma once

#include <memory>
#include <vector>
#include <cstdint>

namespace Tasrovy {

// --- RHI forward declarations ---

class Buffer;
class Pipeline;

// --- CommandList ---

class CommandList : public std::enable_shared_from_this<CommandList> {
public:
    static std::shared_ptr<CommandList> create();

    // --- Lifecycle ---
    void begin();
    void end();

    // --- Render pass ---
    void beginRenderPass(float clearColor[4], bool clearDepth = true);
    void endRenderPass();

    // --- Pipeline ---
    void bindPipeline(Pipeline& pipeline);

    // --- Buffers ---
    void bindVertexBuffer(Buffer& buffer);
    void bindIndexBuffer(Buffer& buffer);

    // --- Descriptors ---
    void bindDescriptorSet(uint32_t setIndex, void* descriptorSet);

    // --- State ---
    void setViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    // --- Draw ---
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1);

    // --- Copy ---
    void copyBuffer(Buffer& src, Buffer& dst, uint64_t size);

    // --- Barrier ---
    void pipelineBarrier(uint32_t srcStage, uint32_t dstStage);

private:
    CommandList() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy
