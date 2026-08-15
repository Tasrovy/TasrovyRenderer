#pragma once

#include "FrameExecutionTypes.h"
#include "CompiledRenderPipeline.h"
#include "RenderFramePlan.h"
#include "CommandList.h"
#include "Image.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tasrovy::RHI {

class CommandList;
class Device;
class Image;
class Buffer;

struct FrameMeshBinding {
    std::shared_ptr<Buffer> vertexBuffer;
    std::shared_ptr<Buffer> indexBuffer;
    uint32_t indexCount = 0;
};

struct FrameTextureBindingOverride {
    std::string resourceName;
    bool previousFrame = false;
};

struct FrameImportedImageBinding {
    std::shared_ptr<Image> image;
    DescriptorImageInfo imageInfo;
    bool useImageInfo = false;
};

struct FrameExecutionBindings {
    std::unordered_map<uint64_t, FrameMeshBinding> meshes;
    std::unordered_map<uint64_t,
        std::unordered_map<std::string, std::shared_ptr<Image>>>
        materialTextures;
    std::shared_ptr<Buffer> skyboxVertexBuffer;
    std::shared_ptr<Buffer> skyboxIndexBuffer;
    uint32_t skyboxIndexCount = 0;
    std::unordered_map<std::string, FrameImportedImageBinding>
        importedImages;
    std::unordered_map<uint64_t,
        std::unordered_map<uint32_t, FrameTextureBindingOverride>>
        textureOverrides;
    std::shared_ptr<Buffer> viewUniform;
    std::shared_ptr<Buffer> objectData;
    std::shared_ptr<Buffer> materialData;
    std::shared_ptr<Buffer> sceneLights;
};

struct FrameExecuteContext {
    Device* device = nullptr;
    CommandList* commandList = nullptr;
    const SwapchainRenderTarget* swapchainTarget = nullptr;
    RenderOverlay* overlay = nullptr;
    const FrameExecutionBindings* bindings = nullptr;
    uint32_t frameIndex = 0;
    bool drawOverlay = false;
    uint64_t timestampQueryPool = 0;
    uint32_t timestampQueryCapacity = 0;
};

struct FrameExecuteResult {
    bool swapchainUsed = false;
    uint32_t timestampQueryCount = 0;
    std::vector<std::string> timestampPassNames;
};

}

namespace Tasrovy::Render {
struct FramePacket;
}

namespace Tasrovy::RHI {

// API-independent frame execution boundary. Renderer code submits a compiled
// frame plan here; the selected RHI backend owns resource resolution and
// concrete barrier translation.
class FrameExecutor {
public:
    using TextureFrames = std::vector<std::shared_ptr<Image>>;
    using TextureMap =
        std::unordered_map<std::string, TextureFrames>;

    FrameExecutor();
    ~FrameExecutor();

    FrameExecutor(const FrameExecutor&) = delete;
    FrameExecutor& operator=(const FrameExecutor&) = delete;
    FrameExecutor(FrameExecutor&&) noexcept;
    FrameExecutor& operator=(FrameExecutor&&) noexcept;

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
