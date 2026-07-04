#include "CommandList.h"
#include "RHIConfig.h"

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanContext.h"
    #include "../RHI/Vulkan/VulkanBuffer.h"
    #include "../RHI/Vulkan/VulkanPipeline.h"
#endif

namespace Tasrovy {

struct CommandList::Impl {
#ifdef TASROVY_API_VULKAN
    VulkanContext* context = nullptr;
#endif
};

std::shared_ptr<CommandList> CommandList::create() {
    auto cmd = std::shared_ptr<CommandList>(new CommandList());
    cmd->impl_ = std::make_unique<Impl>();
    return cmd;
}

void CommandList::begin() {}
void CommandList::end() {}

void CommandList::beginRenderPass(float clearColor[4], bool clearDepth) {}
void CommandList::endRenderPass() {}

void CommandList::bindPipeline(Pipeline& pipeline) {}

void CommandList::bindVertexBuffer(Buffer& buffer) {}
void CommandList::bindIndexBuffer(Buffer& buffer) {}

void CommandList::bindDescriptorSet(uint32_t setIndex, void* descriptorSet) {}

void CommandList::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {}
void CommandList::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {}

void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount) {}
void CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount) {}

void CommandList::copyBuffer(Buffer& src, Buffer& dst, uint64_t size) {}

void CommandList::pipelineBarrier(uint32_t srcStage, uint32_t dstStage) {}

} // namespace Tasrovy
