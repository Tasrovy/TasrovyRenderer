#pragma once

#include "../CommandList.h"
#include "../CompiledRenderPipeline.h"
#include "../Device.h"
#include "../FrameExecutor.h"
#include "../FrameExecutorBackend.h"
#include "../RenderFramePlan.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::RHI {

class Image;
class Buffer;

}

namespace Tasrovy::Render {
struct FramePacket;
}

namespace Tasrovy::RHI {

namespace Vulkan {

class VulkanFrameExecutor final : public IFrameExecutorBackend {
public:
    using TextureFrames =
        std::vector<std::shared_ptr<Image>>;
    using TextureMap =
        std::unordered_map<std::string, TextureFrames>;

    void reset() override;

    void compileExecution(
        Device& device,
        Tasrovy::Render::FramePacket& packet,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config) override;

    void bindFramePacket(Tasrovy::Render::FramePacket& packet) override;
    FrameExecuteResult executeFrame(
        const RenderFrameExecutionPlan& plan,
        const FrameExecuteContext& context) override;

    void rebuildDisplayResources(
        Device& device,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config) override;

    void executePreBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex) override;

    void executePostBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex) override;

    void transition(
        CommandList& commandList,
        Image& image,
        RenderResourceState desired,
        bool forceMemoryBarrier = false) override;

    std::shared_ptr<Image> resolve(
        const std::string& resourceName,
        uint32_t frameIndex,
        bool previousFrame = false) const override;
    std::shared_ptr<Buffer> resolveBuffer(uint64_t resourceId) const override;

    const ResolvedTextureInfo* textureInfo(
        const std::string& resourceName) const override;

    const TextureMap& textures() const override;
    uint64_t allocatedBytes() const override;
    CompiledRenderPipeline& compiledPipeline() override;
    const CompiledRenderPipeline& compiledPipeline() const override;

private:
    void resolveResources(
        Device& device,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config);

    void allocateResources(
        Device& device,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config,
        bool displayOnly);

    void executeBarriers(
        CommandList& commandList,
        const std::vector<RenderResourceTransition>& transitions,
        uint32_t frameIndex);

    TextureMap textures_;
    std::unordered_map<uint64_t, std::shared_ptr<Buffer>> buffers_;
    std::unordered_map<std::string, ResolvedTextureInfo> textureInfos_;
    std::unordered_map<const Image*, RenderResourceState> resourceStates_;
    std::unordered_map<const Image*, uint64_t> imageBytes_;
    uint32_t framesInFlight_ = 0;
    uint64_t allocatedBytes_ = 0;
    CompiledRenderPipeline compiledPipeline_;
};

} // namespace Vulkan
} // namespace Tasrovy::RHI
