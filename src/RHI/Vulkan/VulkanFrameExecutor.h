#pragma once

#include "../CommandList.h"
#include "../CompiledRenderPipeline.h"
#include "../Device.h"
#include "../FrameExecutor.h"
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

class VulkanFrameExecutor {
public:
    using TextureFrames =
        std::vector<std::shared_ptr<Image>>;
    using TextureMap =
        std::unordered_map<std::string, TextureFrames>;

    void reset();

    void compileExecution(
        Device& device,
        Tasrovy::Render::FramePacket& packet,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config);

    void bindFramePacket(Tasrovy::Render::FramePacket& packet);
    FrameExecuteResult executeFrame(
        const RenderFrameExecutionPlan& plan,
        const FrameExecuteContext& context);

    void rebuildDisplayResources(
        Device& device,
        const RenderFrameExecutionPlan& plan,
        const FrameResourceConfig& config);

    void executePreBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex);

    void executePostBarriers(
        CommandList& commandList,
        const RenderPassExecutionPlan& pass,
        uint32_t frameIndex);

    void transition(
        CommandList& commandList,
        Image& image,
        RenderResourceState desired,
        bool forceMemoryBarrier = false);

    std::shared_ptr<Image> resolve(
        const std::string& resourceName,
        uint32_t frameIndex,
        bool previousFrame = false) const;
    std::shared_ptr<Buffer> resolveBuffer(uint64_t resourceId) const;

    const ResolvedTextureInfo* textureInfo(
        const std::string& resourceName) const;

    const TextureMap& textures() const;
    uint64_t allocatedBytes() const;
    CompiledRenderPipeline& compiledPipeline();
    const CompiledRenderPipeline& compiledPipeline() const;

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
