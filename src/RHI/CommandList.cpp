#include "CommandList.h"
#include "CommandListBackend.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI {

struct CommandList::Impl {
    std::unique_ptr<ICommandListBackend> backend;
};

std::shared_ptr<CommandList> CommandList::CreateFromBackend(
    std::unique_ptr<ICommandListBackend> backend) {
    if (!backend) throw std::invalid_argument("CommandList backend is null");
    auto commandList = std::shared_ptr<CommandList>(new CommandList());
    commandList->impl_ = std::make_unique<Impl>();
    commandList->impl_->backend = std::move(backend);
    return commandList;
}

CommandList::~CommandList() = default;
void CommandList::begin() { impl_->backend->begin(); }
void CommandList::end() { impl_->backend->end(); }
void CommandList::useNativeCommandBuffer(uint64_t handle) {
    impl_->backend->attachNativeCommandBuffer(handle);
}
uint64_t CommandList::getNativeCommandBuffer() const {
    return impl_->backend->nativeCommandBuffer();
}
void CommandList::beginRenderPass(Pass& pass) {
    impl_->backend->beginRenderPass(pass);
}
void CommandList::endRenderPass() { impl_->backend->endRenderPass(); }
void CommandList::beginSwapchainRenderPass(
    const SwapchainRenderTarget& target) {
    impl_->backend->beginSwapchainRenderPass(target);
}
void CommandList::endSwapchainRenderPass(
    const SwapchainRenderTarget& target) {
    impl_->backend->endSwapchainRenderPass(target);
}
void CommandList::renderOverlay(
    RenderOverlay& overlay, const SwapchainRenderTarget& target,
    uint64_t frameToken) {
    impl_->backend->renderOverlay(overlay, target, frameToken);
}
void CommandList::bindPipeline(Pipeline& pipeline, bool compute) {
    impl_->backend->bindPipeline(pipeline, compute);
}
void CommandList::bindVertexBuffer(Buffer& buffer) {
    impl_->backend->bindVertexBuffer(buffer);
}
void CommandList::bindIndexBuffer(Buffer& buffer) {
    impl_->backend->bindIndexBuffer(buffer);
}
void CommandList::bindDescriptorSet(
    uint32_t setIndex, const DescriptorSet& descriptorSet) {
    impl_->backend->bindDescriptorSet(setIndex, descriptorSet);
}
void CommandList::setViewport(
    float x, float y, float width, float height,
    float minDepth, float maxDepth) {
    impl_->backend->setViewport(
        x, y, width, height, minDepth, maxDepth);
}
void CommandList::setScissor(
    int32_t x, int32_t y, uint32_t width, uint32_t height) {
    impl_->backend->setScissor(x, y, width, height);
}
void CommandList::setVirtualShadowPage(
    const VirtualShadowPageDesc& page) {
    impl_->backend->setVirtualShadowPage(page);
}
void CommandList::setFrontFace(FrontFace frontFace) {
    impl_->backend->setFrontFace(frontFace);
}
void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount) {
    impl_->backend->draw(vertexCount, instanceCount);
}
void CommandList::drawIndexed(
    uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
    int32_t vertexOffset, uint32_t firstInstance) {
    impl_->backend->drawIndexed(
        indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
void CommandList::drawIndexedIndirect(
    Buffer& indirectBuffer, uint64_t offset,
    uint32_t drawCount, uint32_t stride) {
    impl_->backend->drawIndexedIndirect(
        indirectBuffer, offset, drawCount, stride);
}
void CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    impl_->backend->dispatch(x, y, z);
}
void CommandList::copyBuffer(Buffer& src, Buffer& dst, uint64_t size) {
    impl_->backend->copyBuffer(src, dst, size);
}
void CommandList::pipelineBarrier(
    PipelineStage srcStage, PipelineStage dstStage) {
    impl_->backend->pipelineBarrier(srcStage, dstStage);
}
void CommandList::bufferMemoryBarrier(
    Buffer& buffer, PipelineStage srcStage, PipelineStage dstStage,
    ResourceAccess srcAccess, ResourceAccess dstAccess) {
    impl_->backend->bufferMemoryBarrier(
        buffer, srcStage, dstStage, srcAccess, dstAccess);
}
void CommandList::transitionImage(
    Image& image, ImageLayout oldLayout, ImageLayout newLayout,
    uint32_t aspectMask) {
    impl_->backend->transitionImage(
        image, oldLayout, newLayout, aspectMask);
}
void CommandList::resetTimestampQueryPool(
    uint64_t queryPool, uint32_t queryCount) {
    impl_->backend->resetTimestampQueryPool(queryPool, queryCount);
}
void CommandList::writeTimestamp(
    uint64_t queryPool, uint32_t queryIndex, bool begin) {
    impl_->backend->writeTimestamp(queryPool, queryIndex, begin);
}

} // namespace Tasrovy::RHI
