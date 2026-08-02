#pragma once

#include "Descriptor.h"
#include "../render/FramePacket.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Tasrovy::RHI {
class Buffer;
class DescriptorPool;
class DescriptorSetLayout;
class Pass;
class Pipeline;
}

namespace Tasrovy::RHI {

// Backend objects compiled for one FramePacket pass. Static layout and
// resource plans remain immutable; per-frame parameter bytes, descriptor
// writes and the selected permutation key are refreshed in the packet.
struct CompiledPassResources {
    uint64_t passId = 0;
    size_t packetPassIndex = 0;
    std::optional<Render::FramePassPacket> framePass;
    std::vector<std::shared_ptr<Pass>> rhiPasses;
    std::shared_ptr<Pipeline> gpuPipeline;
    std::unordered_map<uint64_t, std::shared_ptr<Pipeline>> permutations;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<DescriptorPool> descriptorPool;
    std::vector<DescriptorSet> descriptorSets;
    std::vector<std::shared_ptr<Buffer>> uniformBuffers;
    uint32_t descriptorSetsPerFrame = 1;
    bool usesSwapchain = false;
};

class CompiledRenderPipeline {
public:
    using Container = std::vector<CompiledPassResources>;

    void reset();
    CompiledPassResources& add(CompiledPassResources resources);
    CompiledPassResources* find(uint64_t passId);
    const CompiledPassResources* find(uint64_t passId) const;

    Container& passes();
    const Container& passes() const;
    bool empty() const;
    size_t size() const;

private:
    Container passes_;
};

} // namespace Tasrovy::RHI
