#pragma once

#include "FrameExecutor.h"

namespace Tasrovy::RHI {

class IFrameExecutorBackend {
public:
    virtual ~IFrameExecutorBackend() = default;
    virtual void reset() = 0;
    virtual void compileExecution(
        Device& device,
        Tasrovy::Render::FramePacket& packet,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config) = 0;
    virtual void bindFramePacket(Tasrovy::Render::FramePacket& packet) = 0;
    virtual FrameExecuteResult executeFrame(
        const RenderFrameExecutionPlan& plan,
        const FrameExecuteContext& context) = 0;
    virtual void rebuildDisplayResources(
        Device& device,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config) = 0;
    virtual void executePreBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex) = 0;
    virtual void executePostBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex) = 0;
    virtual void transition(
        CommandList& commandList,
        Image& image,
        RenderResourceState desired,
        bool forceMemoryBarrier) = 0;
    virtual std::shared_ptr<Image> resolve(
        const std::string& resourceName,
        uint32_t frameIndex,
        bool previousFrame) const = 0;
    virtual std::shared_ptr<Buffer> resolveBuffer(uint64_t resourceId) const = 0;
    virtual const ResolvedTextureInfo* textureInfo(
        const std::string& resourceName) const = 0;
    virtual const FrameExecutor::TextureMap& textures() const = 0;
    virtual uint64_t allocatedBytes() const = 0;
    virtual CompiledRenderPipeline& compiledPipeline() = 0;
    virtual const CompiledRenderPipeline& compiledPipeline() const = 0;
};

std::unique_ptr<IFrameExecutorBackend> createSelectedFrameExecutorBackend();

} // namespace Tasrovy::RHI
