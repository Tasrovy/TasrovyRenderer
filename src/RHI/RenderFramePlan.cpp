#include "RenderFramePlan.h"

#include "../render/FramePacket.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace Tasrovy::RHI {
namespace {

RenderResourceState stateForAccess(
    Tasrovy::Render::PipelineResourceAccess access) {
    using Access = Tasrovy::Render::PipelineResourceAccess;
    switch (access) {
    case Access::SampledRead:
        return RenderResourceState::ShaderRead;
    case Access::StorageRead:
        return RenderResourceState::StorageRead;
    case Access::StorageWrite:
        return RenderResourceState::StorageWrite;
    case Access::BufferTransferRead:
        return RenderResourceState::TransferRead;
    case Access::BufferTransferWrite:
        return RenderResourceState::TransferWrite;
    case Access::BufferStorageRead:
        return RenderResourceState::StorageRead;
    case Access::BufferStorageWrite:
        return RenderResourceState::StorageWrite;
    case Access::ColorRead:
    case Access::ColorWrite:
        return RenderResourceState::ColorAttachment;
    case Access::DepthRead:
        return RenderResourceState::DepthReadOnly;
    case Access::DepthWrite:
        return RenderResourceState::DepthAttachment;
    }
    return RenderResourceState::Undefined;
}

struct TrackedStateKey {
    Tasrovy::Render::RenderResourceId id = 0;
    bool previousFrame = false;

    bool operator==(const TrackedStateKey&) const = default;
};

struct TrackedStateKeyHash {
    size_t operator()(const TrackedStateKey& key) const {
        return std::hash<uint64_t>{}(
            key.id ^ (key.previousFrame ? (1ull << 63u) : 0ull));
    }
};

struct LifetimeRange {
    size_t first = std::numeric_limits<size_t>::max();
    size_t last = 0;
    bool persistent = false;
    bool storage = false;
    bool buffer = false;
};

FrameTextureFormat toFrameTextureFormat(
    const Tasrovy::Render::PipelineTextureFormat format) {
    using Format = Tasrovy::Render::PipelineTextureFormat;
    switch (format) {
    case Format::RGBA8Unorm:
        return FrameTextureFormat::RGBA8Unorm;
    case Format::RGBA16Float:
        return FrameTextureFormat::RGBA16Float;
    case Format::RG16Float:
        return FrameTextureFormat::RG16Float;
    case Format::Depth32Float:
        return FrameTextureFormat::Depth32Float;
    case Format::Swapchain:
        return FrameTextureFormat::Swapchain;
    }
    return FrameTextureFormat::RGBA16Float;
}

FrameTextureExtent toFrameTextureExtent(
    const Tasrovy::Render::PipelineTextureExtent extent) {
    using Extent = Tasrovy::Render::PipelineTextureExtent;
    switch (extent) {
    case Extent::InternalRelative:
        return FrameTextureExtent::InternalRelative;
    case Extent::DisplayRelative:
        return FrameTextureExtent::DisplayRelative;
    case Extent::Fixed:
        return FrameTextureExtent::Fixed;
    }
    return FrameTextureExtent::InternalRelative;
}

FrameTextureDescription makeFrameTextureDescription(
    const Tasrovy::Render::PipelineTextureDesc& source) {
    FrameTextureDescription result;
    result.name = source.name;
    result.format = toFrameTextureFormat(source.format);
    result.extent = toFrameTextureExtent(source.extent);
    result.widthScale = source.widthScale;
    result.heightScale = source.heightScale;
    result.width = source.width;
    result.height = source.height;
    result.external = source.external;
    return result;
}

uint32_t toRHIVertexFormat(Tasrovy::Render::FrameVertexFormat format) {
    using Format = Tasrovy::Render::FrameVertexFormat;
    switch (format) {
    case Format::Float2: return 103u;
    case Format::Float3: return 106u;
    case Format::Float4: return 109u;
    }
    return 106u;
}

uint32_t toRHICullMode(Tasrovy::Render::CullMode mode) {
    using Mode = Tasrovy::Render::CullMode;
    switch (mode) {
    case Mode::None: return 0u;
    case Mode::Front: return 1u;
    case Mode::Back: return 2u;
    }
    return 2u;
}

uint32_t toRHITopology(Tasrovy::Render::Topology topology) {
    using Topology = Tasrovy::Render::Topology;
    switch (topology) {
    case Topology::TriangleList: return 3u;
    case Topology::LineList: return 1u;
    case Topology::PointList: return 0u;
    }
    return 3u;
}

uint32_t toRHICompare(Tasrovy::Render::DepthTestMode mode) {
    using Mode = Tasrovy::Render::DepthTestMode;
    switch (mode) {
    case Mode::Less: return 1u;
    case Mode::Equal: return 2u;
    case Mode::LessOrEqual: return 3u;
    case Mode::Greater: return 4u;
    case Mode::NotEqual: return 5u;
    }
    return 1u;
}

uint32_t toRHIShaderStages(uint32_t stages) {
    uint32_t result = 0;
    if ((stages & Tasrovy::Render::FrameShaderStageVertex) != 0) {
        result |= 0x00000001u;
    }
    if ((stages & Tasrovy::Render::FrameShaderStageFragment) != 0) {
        result |= 0x00000010u;
    }
    if ((stages & Tasrovy::Render::FrameShaderStageCompute) != 0) {
        result |= 0x00000020u;
    }
    return result;
}

RHIDescriptorTypePlan toRHIDescriptorType(
    Tasrovy::Render::FrameDescriptorType type) {
    using Type = Tasrovy::Render::FrameDescriptorType;
    switch (type) {
    case Type::UniformBuffer: return RHIDescriptorTypePlan::UniformBuffer;
    case Type::CombinedImageSampler:
        return RHIDescriptorTypePlan::CombinedImageSampler;
    case Type::StorageImage: return RHIDescriptorTypePlan::StorageImage;
    case Type::StorageBuffer: return RHIDescriptorTypePlan::StorageBuffer;
    }
    return RHIDescriptorTypePlan::UniformBuffer;
}

RHIAttachmentPlan toRHIAttachment(
    const Tasrovy::Render::FrameAttachmentPacket& attachment) {
    return {
        attachment.resourceId,
        attachment.resourceName,
        static_cast<uint32_t>(attachment.load),
        static_cast<uint32_t>(attachment.store),
        attachment.readOnly,
        attachment.clearDepth
    };
}

RHIPipelinePlan makePipelinePlan(
    const Tasrovy::Render::FramePassPacket& pass) {
    RHIPipelinePlan result;
    result.vertexShaderPath = pass.vertexShader.assetPath;
    result.fragmentShaderPath = pass.fragmentShader.assetPath;
    result.computeShaderPath = pass.computeShader.assetPath;
    result.vertexEntryPoint = pass.vertexShader.entryPoint;
    result.fragmentEntryPoint = pass.fragmentShader.entryPoint;
    result.computeEntryPoint = pass.computeShader.entryPoint;
    result.vertexLayout.stride = pass.vertexLayout.stride;
    for (const auto& attribute : pass.vertexLayout.attributes) {
        result.vertexLayout.attributes.push_back({
            attribute.location,
            toRHIVertexFormat(attribute.format),
            attribute.offset
        });
    }
    result.topology = toRHITopology(pass.state.topology);
    result.cullMode = toRHICullMode(pass.state.cullMode);
    result.depthTest = pass.state.depthTest;
    result.depthWrite = pass.state.depthWrite;
    result.depthCompare = toRHICompare(pass.state.depthTestMode);
    result.blendMode = static_cast<uint32_t>(pass.state.blendMode);
    result.descriptorSets.setsPerFrame =
        pass.descriptorLayout.setsPerFrame;
    result.descriptorSets.uniformByteSize =
        pass.parameters.uniformByteSize;
    for (const auto& binding : pass.descriptorLayout.bindings) {
        result.descriptorLayout.bindings.push_back({
            binding.binding,
            toRHIDescriptorType(binding.type),
            toRHIShaderStages(binding.stages),
            binding.resourceId,
            binding.previousFrame
        });
    }
    if (!result.descriptorLayout.bindings.empty()) {
        uint32_t maxBinding = 0;
        for (const auto& binding : result.descriptorLayout.bindings) {
            maxBinding = std::max(maxBinding, binding.binding);
        }
        std::vector<RHIDescriptorTypePlan> denseBindings(
            static_cast<size_t>(maxBinding) + 1u,
            RHIDescriptorTypePlan::CombinedImageSampler);
        for (const auto& binding : result.descriptorLayout.bindings) {
            denseBindings[binding.binding] = binding.type;
        }
        std::array<uint32_t, 4> counts{};
        for (const auto type : denseBindings) {
            ++counts[static_cast<size_t>(type)];
        }
        for (size_t type = 0; type < counts.size(); ++type) {
            if (counts[type] != 0) {
                result.descriptorPool.sizes.push_back({
                    static_cast<RHIDescriptorTypePlan>(type),
                    counts[type]
                });
            }
        }
    }
    for (const auto& permutation : pass.permutations) {
        result.permutations.push_back({
            permutation.key,
            permutation.vertexShader.assetPath,
            permutation.fragmentShader.assetPath,
            permutation.computeShader.assetPath,
            permutation.vertexShader.entryPoint,
            permutation.fragmentShader.entryPoint,
            permutation.computeShader.entryPoint
        });
    }
    return result;
}

bool allocationCompatible(
    const FrameTextureDescription& lhs,
    const FrameTextureDescription& rhs) {
    return lhs.format == rhs.format &&
        lhs.extent == rhs.extent &&
        lhs.widthScale == rhs.widthScale &&
        lhs.heightScale == rhs.heightScale &&
        lhs.width == rhs.width &&
        lhs.height == rhs.height;
}

} // namespace

RenderFrameExecutionPlan RHIFrameCompiler::compile(
    const Tasrovy::Render::FramePacket& packet) const {
    RenderFrameExecutionPlan plan;
    plan.frameNumber = packet.frameNumber;
    plan.diagnostics = packet.diagnostics;

    if (!packet.valid()) {
        if (plan.diagnostics.empty()) {
            plan.diagnostics.emplace_back(
                "RHIFrameCompiler rejected an invalid FramePacket");
        }
        return plan;
    }

    std::unordered_map<
        Tasrovy::Render::RenderResourceId,
        const Tasrovy::Render::FrameTexturePacket*> textures;
    for (const auto& texture : packet.textures) {
        if (texture.id == 0 || texture.description.name.empty()) {
            plan.diagnostics.emplace_back(
                "RHI frame plan contains an invalid texture declaration");
            continue;
        }
        if (!textures.emplace(texture.id, &texture).second) {
            plan.diagnostics.push_back(
                "RHI frame plan contains duplicate resource id for '" +
                texture.description.name + "'");
        }
    }
    std::unordered_map<
        Tasrovy::Render::RenderResourceId,
        const Tasrovy::Render::FrameBufferPacket*> buffers;
    for (const auto& buffer : packet.buffers) {
        if (buffer.id == 0 || buffer.description.name.empty() ||
            buffer.description.byteSize == 0) {
            plan.diagnostics.emplace_back(
                "RHI frame plan contains an invalid buffer declaration");
            continue;
        }
        if (textures.contains(buffer.id) ||
            !buffers.emplace(buffer.id, &buffer).second) {
            plan.diagnostics.push_back(
                "RHI frame plan contains duplicate resource id for '" +
                buffer.description.name + "'");
        }
    }

    std::unordered_map<
        TrackedStateKey,
        RenderResourceState,
        TrackedStateKeyHash> currentStates;
    for (const auto& [id, buffer] : buffers) {
        if (buffer->description.external ||
            buffer->description.hostVisible) {
            currentStates.emplace(
                TrackedStateKey{id, false},
                RenderResourceState::HostWrite);
        }
    }
    std::unordered_map<
        Tasrovy::Render::RenderResourceId,
        LifetimeRange> lifetimes;

    plan.passes.reserve(packet.passes.size());
    for (size_t passIndex = 0;
         passIndex < packet.passes.size();
         ++passIndex) {
        const auto& packetPass = packet.passes[passIndex];
        RenderPassExecutionPlan passPlan;
        passPlan.passId = packetPass.id;
        passPlan.packetPassIndex = passIndex;
        passPlan.name = packetPass.name;
        passPlan.execution = static_cast<uint32_t>(packetPass.execution);
        passPlan.passType = static_cast<uint32_t>(packetPass.type);
        passPlan.clearColor = {
            packetPass.state.clearColor.x,
            packetPass.state.clearColor.y,
            packetPass.state.clearColor.z,
            packetPass.state.clearColor.w
        };
        passPlan.pipeline = makePipelinePlan(packetPass);
        for (const auto& attachment : packetPass.colorAttachments) {
            passPlan.colorAttachments.push_back(
                toRHIAttachment(attachment));
        }
        if (packetPass.depthAttachment) {
            passPlan.depthAttachment =
                toRHIAttachment(*packetPass.depthAttachment);
        }
        passPlan.drawCount =
            static_cast<uint32_t>(packetPass.draws.size());

        struct DesiredState {
            std::string name;
            RenderResourceState state = RenderResourceState::Undefined;
            bool previousFrame = false;
            bool write = false;
        };
        std::unordered_map<TrackedStateKey, DesiredState, TrackedStateKeyHash>
            desiredStates;

        const auto recordUse = [&](
            const Tasrovy::Render::FrameResourceUse& use,
            bool isWrite) {
            const bool bufferUse =
                Tasrovy::Render::isBufferAccess(use.access);
            if ((bufferUse && !buffers.contains(use.id)) ||
                (!bufferUse && !textures.contains(use.id))) {
                plan.diagnostics.push_back(
                    "Pass '" + packetPass.name +
                    "' references undeclared resource '" +
                    use.resourceName + "'");
                return;
            }
            if (bufferUse &&
                (use.access ==
                     Tasrovy::Render::PipelineResourceAccess::BufferStorageRead ||
                 use.access ==
                     Tasrovy::Render::PipelineResourceAccess::BufferStorageWrite) &&
                (buffers.at(use.id)->description.usageFlags &
                    Tasrovy::Render::PipelineBufferUsageStorage) == 0) {
                plan.diagnostics.push_back(
                    "Pass '" + packetPass.name +
                    "' uses non-storage buffer '" + use.resourceName +
                    "' as a shader resource");
                return;
            }

            const TrackedStateKey key{use.id, use.previousFrame};
            const auto desired = stateForAccess(use.access);
            auto [found, inserted] = desiredStates.try_emplace(
                key,
                DesiredState{
                    use.resourceName,
                    desired,
                    use.previousFrame,
                    isWrite
                });
            if (!inserted && isWrite) {
                found->second.state = desired;
                found->second.write = true;
            }

            auto& lifetime = lifetimes[use.id];
            lifetime.first = std::min(lifetime.first, passIndex);
            lifetime.last = std::max(lifetime.last, passIndex);
            lifetime.persistent |= use.previousFrame;
            lifetime.storage |=
                use.access ==
                    Tasrovy::Render::PipelineResourceAccess::StorageRead ||
                use.access ==
                    Tasrovy::Render::PipelineResourceAccess::StorageWrite;
            lifetime.storage |=
                use.access ==
                    Tasrovy::Render::PipelineResourceAccess::BufferStorageRead ||
                use.access ==
                    Tasrovy::Render::PipelineResourceAccess::BufferStorageWrite;
            lifetime.buffer = bufferUse;
        };

        for (const auto& read : packetPass.reads) {
            recordUse(read, false);
        }
        for (const auto& write : packetPass.writes) {
            recordUse(write, true);
        }

        for (const auto& [key, desired] : desiredStates) {
            const auto current = currentStates.find(key);
            const auto before = current == currentStates.end()
                ? RenderResourceState::Undefined
                : current->second;
            passPlan.preTransitions.push_back({
                key.id,
                desired.name,
                before,
                desired.state,
                desired.previousFrame,
                desired.write,
                lifetimes[key.id].buffer
                    ? RenderResourceKind::Buffer
                    : RenderResourceKind::Texture
            });
            currentStates[key] = desired.state;
        }

        std::sort(
            passPlan.preTransitions.begin(),
            passPlan.preTransitions.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.previousFrame != rhs.previousFrame) {
                    return lhs.previousFrame < rhs.previousFrame;
                }
                return lhs.resourceId < rhs.resourceId;
            });
        plan.passes.push_back(std::move(passPlan));
    }

    for (const auto& [id, texture] : textures) {
        if (texture->description.format !=
            Tasrovy::Render::PipelineTextureFormat::Swapchain) {
            continue;
        }
        const auto lifetime = lifetimes.find(id);
        if (lifetime == lifetimes.end() ||
            lifetime->second.last >= plan.passes.size()) {
            continue;
        }
        const TrackedStateKey key{id, false};
        const auto current = currentStates.find(key);
        const auto before = current == currentStates.end()
            ? RenderResourceState::Undefined
            : current->second;
        if (before != RenderResourceState::Present) {
            plan.passes[lifetime->second.last].postTransitions.push_back({
                id,
                texture->description.name,
                before,
                RenderResourceState::Present,
                false,
                false
            });
            currentStates[key] = RenderResourceState::Present;
        }
    }

    plan.resources.reserve(lifetimes.size());
    for (const auto& [id, lifetime] : lifetimes) {
        const auto texture = textures.find(id);
        if (texture == textures.end()) {
            continue;
        }
        plan.resources.push_back({
            id,
            texture->second->description.name,
            lifetime.first,
            lifetime.last,
            texture->second->description.external,
            lifetime.persistent,
            lifetime.storage,
            -1,
            makeFrameTextureDescription(texture->second->description)
        });
    }
    plan.buffers.reserve(buffers.size());
    for (const auto& [id, buffer] : buffers) {
        const auto lifetime = lifetimes.find(id);
        if (lifetime == lifetimes.end()) {
            continue;
        }
        plan.buffers.push_back({
            id,
            buffer->description.name,
            lifetime->second.first,
            lifetime->second.last,
            buffer->description.external ||
                buffer->description.hostVisible,
            FrameBufferDescription{
                buffer->description.name,
                buffer->description.byteSize,
                buffer->description.usageFlags,
                buffer->description.hostVisible,
                buffer->description.external
            }
        });
    }
    std::sort(
        plan.buffers.begin(), plan.buffers.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.firstPass != rhs.firstPass) {
                return lhs.firstPass < rhs.firstPass;
            }
            return lhs.resourceId < rhs.resourceId;
        });
    std::sort(
        plan.resources.begin(),
        plan.resources.end(),
        [](const auto& lhs, const auto& rhs) {
            if (lhs.firstPass != rhs.firstPass) {
                return lhs.firstPass < rhs.firstPass;
            }
            return lhs.resourceId < rhs.resourceId;
        });

    struct AllocationSlot {
        FrameTextureDescription description;
        size_t lastPass = 0;
        bool storage = false;
    };
    std::vector<AllocationSlot> allocationSlots;
    for (auto& resource : plan.resources) {
        if (resource.external ||
            resource.persistent ||
            resource.resourceName == "VirtualShadowAtlas") {
            continue;
        }
        for (size_t slot = 0; slot < allocationSlots.size(); ++slot) {
            if (allocationSlots[slot].lastPass < resource.firstPass &&
                allocationSlots[slot].storage == resource.storage &&
                allocationCompatible(
                    allocationSlots[slot].description,
                    resource.description)) {
                resource.allocationSlot = static_cast<int32_t>(slot);
                allocationSlots[slot].lastPass = resource.lastPass;
                break;
            }
        }
        if (resource.allocationSlot < 0) {
            resource.allocationSlot =
                static_cast<int32_t>(allocationSlots.size());
            allocationSlots.push_back({
                resource.description,
                resource.lastPass,
                resource.storage
            });
        }
    }

    if (!plan.valid()) {
        // Diagnostics are preserved, but an invalid plan must never expose a
        // partially executable pass/resource list to a scheduler or backend.
        plan.passes.clear();
        plan.resources.clear();
        plan.buffers.clear();
    }
    return plan;
}

} // namespace Tasrovy::RHI
