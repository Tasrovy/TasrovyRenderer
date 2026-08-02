#include "FrameExecutor.h"

#include "RHIConfig.h"

#ifdef TASROVY_API_VULKAN
#include "Vulkan/VulkanFrameExecutor.h"
#endif

#include <utility>

namespace Tasrovy::RHI {

struct FrameExecutor::Impl {
#ifdef TASROVY_API_VULKAN
    Vulkan::VulkanFrameExecutor backend;
#endif
};

FrameExecutor::FrameExecutor()
    : impl_(std::make_unique<Impl>()) {
}

FrameExecutor::~FrameExecutor() = default;
FrameExecutor::FrameExecutor(FrameExecutor&&) noexcept = default;
FrameExecutor& FrameExecutor::operator=(FrameExecutor&&) noexcept = default;

void FrameExecutor::reset() {
    impl_->backend.reset();
}

void FrameExecutor::compileExecution(
    Device& device,
    Tasrovy::Render::FramePacket& packet,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config) {
    impl_->backend.compileExecution(device, packet, plan, config);
}

void FrameExecutor::bindFramePacket(
    Tasrovy::Render::FramePacket& packet) {
    impl_->backend.bindFramePacket(packet);
}

FrameExecuteResult FrameExecutor::executeFrame(
    const RenderFrameExecutionPlan& plan,
    const FrameExecuteContext& context) {
    return impl_->backend.executeFrame(plan, context);
}

void FrameExecutor::rebuildDisplayResources(
    Device& device,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config) {
    impl_->backend.rebuildDisplayResources(device, plan, config);
}

void FrameExecutor::executePreBarriers(
    CommandList& commandList,
    const RenderPassExecutionPlan& pass,
    uint32_t frameIndex) {
    impl_->backend.executePreBarriers(commandList, pass, frameIndex);
}

void FrameExecutor::executePostBarriers(
    CommandList& commandList,
    const RenderPassExecutionPlan& pass,
    uint32_t frameIndex) {
    impl_->backend.executePostBarriers(commandList, pass, frameIndex);
}

void FrameExecutor::transition(
    CommandList& commandList,
    Image& image,
    RenderResourceState desired,
    bool forceMemoryBarrier) {
    impl_->backend.transition(
        commandList, image, desired, forceMemoryBarrier);
}

std::shared_ptr<Image> FrameExecutor::resolve(
    const std::string& resourceName,
    uint32_t frameIndex,
    bool previousFrame) const {
    return impl_->backend.resolve(
        resourceName, frameIndex, previousFrame);
}

std::shared_ptr<Buffer> FrameExecutor::resolveBuffer(
    uint64_t resourceId) const {
    return impl_->backend.resolveBuffer(resourceId);
}

const ResolvedTextureInfo* FrameExecutor::textureInfo(
    const std::string& resourceName) const {
    return impl_->backend.textureInfo(resourceName);
}

const FrameExecutor::TextureMap& FrameExecutor::textures() const {
    return impl_->backend.textures();
}

uint64_t FrameExecutor::allocatedBytes() const {
    return impl_->backend.allocatedBytes();
}

CompiledRenderPipeline& FrameExecutor::compiledPipeline() {
    return impl_->backend.compiledPipeline();
}

const CompiledRenderPipeline& FrameExecutor::compiledPipeline() const {
    return impl_->backend.compiledPipeline();
}

} // namespace Tasrovy::RHI
