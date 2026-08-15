#include "VulkanFrameExecutor.h"
#include "VulkanRenderFramePlan.h"

#include "../Image.h"
#include "../Buffer.h"
#include "../../render/FramePacket.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Tasrovy::RHI::Vulkan {
namespace {

RenderTextureFormat toRHIFormat(
    FrameTextureFormat format) {
    using Format = FrameTextureFormat;
    switch (format) {
    case Format::RGBA8Unorm:
        return RenderTextureFormat::RGBA8Unorm;
    case Format::RGBA16Float:
        return RenderTextureFormat::RGBA16Float;
    case Format::RG16Float:
        return RenderTextureFormat::RG16Float;
    case Format::Depth32Float:
        return RenderTextureFormat::Depth32Float;
    case Format::Swapchain:
        return RenderTextureFormat::Swapchain;
    }
    return RenderTextureFormat::RGBA8Unorm;
}

uint32_t resolveExtent(
    FrameTextureExtent extent,
    uint32_t fixed,
    float scale,
    uint32_t display,
    uint32_t internal) {
    using Extent = FrameTextureExtent;
    if (extent == Extent::Fixed) {
        return std::max(fixed, 1u);
    }
    const uint32_t base =
        extent == Extent::DisplayRelative ? display : internal;
    return std::max(
        1u,
        static_cast<uint32_t>(static_cast<float>(base) * scale));
}

uint32_t bytesPerPixel(RenderTextureFormat format) {
    switch (format) {
    case RenderTextureFormat::RGBA16Float:
        return 8;
    case RenderTextureFormat::RG16Float:
        return 4;
    case RenderTextureFormat::Depth32Float:
        return 4;
    case RenderTextureFormat::RGBA8Unorm:
        return 4;
    case RenderTextureFormat::Swapchain:
        return 0;
    }
    return 0;
}

bool isDisplayResource(
    const FrameTextureDescription& description) {
    return description.external ||
        description.extent ==
            FrameTextureExtent::DisplayRelative;
}

DescriptorResourceType toDescriptorType(RHIDescriptorTypePlan type) {
    switch (type) {
    case RHIDescriptorTypePlan::UniformBuffer:
        return DescriptorResourceType::UniformBuffer;
    case RHIDescriptorTypePlan::CombinedImageSampler:
        return DescriptorResourceType::CombinedImageSampler;
    case RHIDescriptorTypePlan::StorageImage:
        return DescriptorResourceType::StorageImage;
    case RHIDescriptorTypePlan::StorageBuffer:
        return DescriptorResourceType::StorageBuffer;
    }
    return DescriptorResourceType::UniformBuffer;
}

DescriptorResourceType toDescriptorType(
    Tasrovy::Render::FrameDescriptorType type) {
    using Type = Tasrovy::Render::FrameDescriptorType;
    switch (type) {
    case Type::UniformBuffer:
        return DescriptorResourceType::UniformBuffer;
    case Type::CombinedImageSampler:
        return DescriptorResourceType::CombinedImageSampler;
    case Type::StorageImage:
        return DescriptorResourceType::StorageImage;
    case Type::StorageBuffer:
        return DescriptorResourceType::StorageBuffer;
    }
    return DescriptorResourceType::UniformBuffer;
}

RHIAttachmentLoad toAttachmentLoad(uint32_t load) {
    switch (load) {
    case 1u: return RHIAttachmentLoad::Load;
    case 2u: return RHIAttachmentLoad::Discard;
    default: return RHIAttachmentLoad::Clear;
    }
}

RHIAttachmentStore toAttachmentStore(uint32_t store) {
    return store == 1u
        ? RHIAttachmentStore::Discard
        : RHIAttachmentStore::Store;
}

bool isDisplayPass(
    const RenderPassExecutionPlan& pass,
    const std::unordered_map<std::string, ResolvedTextureInfo>& infos) {
    const auto displayAttachment = [&](const RHIAttachmentPlan& attachment) {
        const auto found = infos.find(attachment.resourceName);
        return found != infos.end() && found->second.external;
    };
    if (std::any_of(
            pass.colorAttachments.begin(), pass.colorAttachments.end(),
            displayAttachment)) {
        return true;
    }
    return pass.depthAttachment && displayAttachment(*pass.depthAttachment);
}

bool passUsesResourceIn(
    const RenderPassExecutionPlan& pass,
    const std::unordered_set<std::string>& resourceNames) {
    const auto selected = [&](const RHIAttachmentPlan& attachment) {
        return resourceNames.contains(attachment.resourceName);
    };
    return std::any_of(
            pass.colorAttachments.begin(),
            pass.colorAttachments.end(),
            selected) ||
        (pass.depthAttachment && selected(*pass.depthAttachment));
}

uint32_t toRHIBufferUsage(uint32_t usage) {
    using namespace Tasrovy::Render;
    uint32_t result = 0;
    if ((usage & PipelineBufferUsageTransferSource) != 0) result |= 0x4u;
    if ((usage & PipelineBufferUsageVertex) != 0) result |= 0x1u;
    if ((usage & PipelineBufferUsageIndex) != 0) result |= 0x2u;
    if ((usage & PipelineBufferUsageUniform) != 0) result |= 0x10u;
    if ((usage & PipelineBufferUsageStorage) != 0) result |= 0x20u;
    if ((usage & PipelineBufferUsageIndirect) != 0) result |= 0x40u;
    return result;
}

} // namespace

void VulkanFrameExecutor::reset() {
    textures_.clear();
    buffers_.clear();
    textureInfos_.clear();
    resourceStates_.clear();
    imageBytes_.clear();
    framesInFlight_ = 0;
    allocatedBytes_ = 0;
    compiledPipeline_.reset();
}

void VulkanFrameExecutor::resolveResources(
    Device& device,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config) {
    reset();
    allocateResources(device, plan, config, false);
    allocateResources(device, plan, config, true);
}

void VulkanFrameExecutor::compileExecution(
    Device& device,
    Tasrovy::Render::FramePacket& packet,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config) {
    if (!packet.valid() || !plan.valid()) {
        reset();
        throw std::invalid_argument(
            "VulkanFrameExecutor rejected an invalid frame execution plan");
    }
    resolveResources(device, plan, config);
    for (const auto& buffer : plan.buffers) {
        buffers_[buffer.resourceId] = device.retainResource(
            config.sceneScope,
            device.createBuffer({
                buffer.description.byteSize,
                toRHIBufferUsage(buffer.description.usageFlags),
                buffer.description.hostVisible
            }));
    }
    compiledPipeline_.reset();

    std::unordered_map<uint64_t, std::string> resourceNames;
    std::unordered_set<std::string> displayResourceNames;
    resourceNames.reserve(plan.resources.size());
    for (const auto& resource : plan.resources) {
        resourceNames.emplace(resource.resourceId, resource.resourceName);
        if (isDisplayResource(resource.description)) {
            displayResourceNames.emplace(resource.resourceName);
        }
    }

    for (const auto& passPlan : plan.passes) {
        CompiledPassResources compiled;
        compiled.passId = passPlan.passId;
        compiled.packetPassIndex = passPlan.packetPassIndex;
        compiled.framePass = passPlan.packetPassIndex < packet.passes.size()
            ? std::optional<Tasrovy::Render::FramePassPacket>(
                packet.passes[passPlan.packetPassIndex])
            : std::nullopt;
        compiled.descriptorSetsPerFrame = std::max(
            passPlan.pipeline.descriptorSets.setsPerFrame, 1u);
        compiled.usesSwapchain = isDisplayPass(passPlan, textureInfos_);
        const auto objectScope = passUsesResourceIn(
                passPlan, displayResourceNames)
            ? config.displayScope
            : config.sceneScope;

        const auto& descriptorPlan = passPlan.pipeline.descriptorLayout;
        if (!descriptorPlan.bindings.empty()) {
            uint32_t maxBinding = 0;
            for (const auto& binding : descriptorPlan.bindings) {
                maxBinding = std::max(maxBinding, binding.binding);
            }
            DescriptorSetDesc layoutDesc;
            layoutDesc.bindingTypes.assign(
                static_cast<size_t>(maxBinding) + 1u,
                DescriptorResourceType::CombinedImageSampler);
            layoutDesc.stageFlags.assign(
                static_cast<size_t>(maxBinding) + 1u,
                ShaderStageFragment);
            for (const auto& binding : descriptorPlan.bindings) {
                layoutDesc.bindingTypes[binding.binding] =
                    toDescriptorType(binding.type);
                layoutDesc.stageFlags[binding.binding] = binding.stages;
            }
            compiled.descriptorSetLayout = device.retainResource(
                config.sceneScope,
                device.createDescriptorSetLayout(layoutDesc));

            const uint32_t setCount =
                std::max(config.framesInFlight, 1u) *
                compiled.descriptorSetsPerFrame;
            std::vector<DescriptorPoolSizeDesc> poolSizes;
            for (const auto& size :
                 passPlan.pipeline.descriptorPool.sizes) {
                poolSizes.push_back({
                    toDescriptorType(size.type),
                    size.descriptorsPerSet * setCount
                });
            }
            compiled.descriptorPool = device.retainResource(
                config.sceneScope,
                device.createDescriptorPool(setCount, poolSizes));
            compiled.descriptorSets.resize(setCount);
            const uint32_t uniformByteSize =
                passPlan.pipeline.descriptorSets.uniformByteSize;
            if (uniformByteSize > 0) {
                compiled.uniformBuffers.resize(
                    std::max(config.framesInFlight, 1u));
                for (auto& uniformBuffer : compiled.uniformBuffers) {
                    uniformBuffer = device.retainResource(
                        config.sceneScope,
                        device.createUniformBuffer(uniformByteSize));
                }
            }
            for (uint32_t set = 0; set < setCount; ++set) {
                compiled.descriptorSets[set] = device.allocateDescriptorSet(
                    *compiled.descriptorPool,
                    *compiled.descriptorSetLayout);
                std::vector<DescriptorWriteDesc> writes;
                const uint32_t frame =
                    set / compiled.descriptorSetsPerFrame;
                if (uniformByteSize > 0) {
                    writes.push_back({
                        0,
                        DescriptorResourceType::UniformBuffer,
                        compiled.uniformBuffers[frame]
                    });
                }
                if (compiled.framePass) {
                for (const auto& packetWrite :
                     compiled.framePass->descriptorWrites) {
                    if (packetWrite.source !=
                            Tasrovy::Render::FrameDescriptorSource::RenderTexture ||
                        packetWrite.resourceId == 0 ||
                        packetWrite.binding == 0) {
                        continue;
                    }
                    const auto name = resourceNames.find(packetWrite.resourceId);
                    if (name == resourceNames.end()) {
                        continue;
                    }
                    const auto image = resolve(
                        packetWrite.resourceName.empty()
                            ? name->second
                            : packetWrite.resourceName,
                        frame,
                        packetWrite.previousFrame);
                    if (!image) {
                        continue;
                    }
                    DescriptorWriteDesc write;
                    write.binding = packetWrite.binding;
                    write.type = toDescriptorType(packetWrite.type);
                    if (write.type == DescriptorResourceType::StorageImage) {
                        write.imageInfo = image->getDescriptorInfoForStorage();
                    } else {
                        write.image = image;
                    }
                    writes.push_back(std::move(write));
                }
                }
                device.updateDescriptorSet(
                    compiled.descriptorSets[set], writes);
            }
        }

        compiled.rhiPasses.reserve(std::max(config.framesInFlight, 1u));
        for (uint32_t frame = 0;
             frame < std::max(config.framesInFlight, 1u);
             ++frame) {
            PassDesc passDesc;
            passDesc.name = passPlan.name;
            passDesc.width = config.displayWidth;
            passDesc.height = config.displayHeight;
            for (const auto& attachment : passPlan.colorAttachments) {
                if (const auto found = textureInfos_.find(
                        attachment.resourceName);
                    found != textureInfos_.end()) {
                    passDesc.width = found->second.width;
                    passDesc.height = found->second.height;
                }
                passDesc.colorAttachments.push_back({
                    attachment.resourceName,
                    resolve(attachment.resourceName, frame),
                    toAttachmentLoad(attachment.load),
                    toAttachmentStore(attachment.store),
                    false,
                    Tasrovy::Base::TSVec4f(
                        passPlan.clearColor[0], passPlan.clearColor[1],
                        passPlan.clearColor[2], passPlan.clearColor[3])
                });
            }
            if (passPlan.depthAttachment) {
                const auto& attachment = *passPlan.depthAttachment;
                if (const auto found = textureInfos_.find(
                        attachment.resourceName);
                    found != textureInfos_.end()) {
                    passDesc.width = found->second.width;
                    passDesc.height = found->second.height;
                }
                passDesc.depthAttachment = RHIAttachmentDesc{
                    attachment.resourceName,
                    resolve(attachment.resourceName, frame),
                    toAttachmentLoad(attachment.load),
                    toAttachmentStore(attachment.store),
                    attachment.readOnly,
                    Tasrovy::Base::TSVec4f(0.0f),
                    attachment.clearDepth
                };
            }
            compiled.rhiPasses.push_back(device.retainResource(
                objectScope, device.createPass(std::move(passDesc))));
        }

        const auto& pipelinePlan = passPlan.pipeline;
        if (!pipelinePlan.computeShaderPath.empty()) {
            ComputePipelineDesc desc;
            desc.shaderPath = pipelinePlan.computeShaderPath;
            desc.entryPoint = pipelinePlan.computeEntryPoint.empty()
                ? "CSMain"
                : pipelinePlan.computeEntryPoint;
            desc.descriptorSetLayout = compiled.descriptorSetLayout;
            compiled.gpuPipeline = device.retainResource(
                config.sceneScope, device.createComputePipeline(desc));
            for (const auto& permutation : pipelinePlan.permutations) {
                ComputePipelineDesc permutationDesc = desc;
                if (!permutation.computeShaderPath.empty()) {
                    permutationDesc.shaderPath =
                        permutation.computeShaderPath;
                }
                if (!permutation.computeEntryPoint.empty()) {
                    permutationDesc.entryPoint =
                        permutation.computeEntryPoint;
                }
                compiled.permutations.emplace(
                    permutation.key,
                    device.retainResource(
                        config.sceneScope,
                        device.createComputePipeline(permutationDesc)));
            }
        } else if (!pipelinePlan.vertexShaderPath.empty() &&
                   !pipelinePlan.fragmentShaderPath.empty()) {
            const auto buildGraphicsDesc = [&](
                const RHIPipelinePermutationPlan* permutation) {
                PipelineDesc desc;
                desc.vertShaderPath = permutation &&
                    !permutation->vertexShaderPath.empty()
                    ? permutation->vertexShaderPath
                    : pipelinePlan.vertexShaderPath;
                desc.fragShaderPath = permutation &&
                    !permutation->fragmentShaderPath.empty()
                    ? permutation->fragmentShaderPath
                    : pipelinePlan.fragmentShaderPath;
                const auto& vertexEntry = permutation &&
                    !permutation->vertexEntryPoint.empty()
                    ? permutation->vertexEntryPoint
                    : pipelinePlan.vertexEntryPoint;
                const auto& fragmentEntry = permutation &&
                    !permutation->fragmentEntryPoint.empty()
                    ? permutation->fragmentEntryPoint
                    : pipelinePlan.fragmentEntryPoint;
                desc.vertEntryPoint = vertexEntry.empty()
                    ? "VSMain"
                    : vertexEntry;
                desc.fragEntryPoint = fragmentEntry.empty()
                    ? "PSMain"
                    : fragmentEntry;
                desc.vertexStride = pipelinePlan.vertexLayout.stride;
                for (const auto& attribute :
                    pipelinePlan.vertexLayout.attributes) {
                    desc.attributeLocations.push_back(attribute.location);
                    desc.attributeFormats.push_back(attribute.format);
                    desc.attributeOffsets.push_back(attribute.offset);
                }
                desc.topology = pipelinePlan.topology;
                desc.cullMode = pipelinePlan.cullMode;
                desc.depthTest = pipelinePlan.depthTest;
                desc.depthWrite = pipelinePlan.depthWrite;
                desc.depthCompareOp = pipelinePlan.depthCompare;
                desc.blendMode = pipelinePlan.blendMode;
                desc.useMSAA = compiled.usesSwapchain;
                desc.descriptorSetLayout = compiled.descriptorSetLayout;
                for (const auto& attachment : passPlan.colorAttachments) {
                    const auto found = textureInfos_.find(
                        attachment.resourceName);
                    if (found != textureInfos_.end()) {
                        desc.colorAttachmentFormats.push_back(
                            device.resolveRenderTextureFormat(
                                found->second.format));
                    }
                }
                if (compiled.usesSwapchain) {
                    desc.depthAttachmentFormat = device.getDepthFormat();
                }
                if (passPlan.depthAttachment) {
                    const auto found = textureInfos_.find(
                        passPlan.depthAttachment->resourceName);
                    if (found != textureInfos_.end()) {
                        desc.depthAttachmentFormat =
                            device.resolveRenderTextureFormat(
                                found->second.format);
                    }
                }
                return desc;
            };
            compiled.gpuPipeline = device.retainResource(
                config.sceneScope,
                device.createGraphicsPipeline(buildGraphicsDesc(nullptr)));
            for (const auto& permutation : pipelinePlan.permutations) {
                compiled.permutations.emplace(
                    permutation.key,
                    device.retainResource(
                        config.sceneScope,
                        device.createGraphicsPipeline(
                            buildGraphicsDesc(&permutation))));
            }
        }
        compiledPipeline_.add(std::move(compiled));
    }
}

void VulkanFrameExecutor::bindFramePacket(
    Tasrovy::Render::FramePacket& packet) {
    for (auto& compiled : compiledPipeline_.passes()) {
        compiled.framePass =
            compiled.packetPassIndex < packet.passes.size() &&
            packet.passes[compiled.packetPassIndex].id == compiled.passId
                ? std::optional<Tasrovy::Render::FramePassPacket>(
                    packet.passes[compiled.packetPassIndex])
                : std::nullopt;
        if (!compiled.framePass) {
            const auto found = std::find_if(
                packet.passes.begin(), packet.passes.end(),
                [&](const auto& pass) { return pass.id == compiled.passId; });
            if (found != packet.passes.end()) {
                compiled.framePass = *found;
            }
        }
    }
}

FrameExecuteResult VulkanFrameExecutor::executeFrame(
    const RenderFrameExecutionPlan& plan,
    const FrameExecuteContext& context) {
    FrameExecuteResult result;
    if (!plan.valid() || !context.device || !context.commandList ||
        !context.bindings) {
        throw std::invalid_argument(
            "VulkanFrameExecutor rejected an incomplete frame context");
    }

    auto& device = *context.device;
    auto& commandList = *context.commandList;
    const auto& bindings = *context.bindings;
    const uint32_t frameIndex = context.frameIndex;
    bool swapchainPassOpen = false;
    bool drawOverlay = context.drawOverlay;

    const auto descriptorWrites = [&] (
        CompiledPassResources& compiled,
        const Tasrovy::Render::FramePassPacket& packetPass,
        uint32_t descriptorIndex,
        uint64_t materialId) {
        if (packetPass.descriptorWrites.empty()) {
            return;
        }
        if (descriptorIndex >= compiled.descriptorSets.size()) {
            throw std::out_of_range(
                "Frame descriptor index exceeds the compiled set count");
        }
        std::vector<DescriptorWriteDesc> writes;
        writes.reserve(packetPass.descriptorWrites.size());
        for (const auto& packetWrite : packetPass.descriptorWrites) {
            DescriptorWriteDesc write;
            write.binding = packetWrite.binding;
            switch (packetWrite.source) {
            case Tasrovy::Render::FrameDescriptorSource::UniformData:
                if (frameIndex >= compiled.uniformBuffers.size()) {
                    throw std::invalid_argument(
                        "Uniform descriptor has no compiled buffer");
                }
                write.type = DescriptorResourceType::UniformBuffer;
                write.buffer = compiled.uniformBuffers[frameIndex];
                break;
            case Tasrovy::Render::FrameDescriptorSource::ViewUniform:
                write.type = DescriptorResourceType::UniformBuffer;
                write.buffer = bindings.viewUniform;
                break;
            case Tasrovy::Render::FrameDescriptorSource::ObjectData:
                write.type = DescriptorResourceType::StorageBuffer;
                write.buffer = bindings.objectData;
                break;
            case Tasrovy::Render::FrameDescriptorSource::MaterialData:
                write.type = DescriptorResourceType::StorageBuffer;
                write.buffer = bindings.materialData;
                break;
            case Tasrovy::Render::FrameDescriptorSource::SceneLights:
                write.type = DescriptorResourceType::StorageBuffer;
                write.buffer = bindings.sceneLights;
                break;
            case Tasrovy::Render::FrameDescriptorSource::RenderTexture: {
                std::string resourceName = packetWrite.resourceName;
                bool previousFrame = packetWrite.previousFrame;
                if (const auto passOverrides =
                        bindings.textureOverrides.find(packetPass.id);
                    passOverrides != bindings.textureOverrides.end()) {
                    if (const auto bindingOverride =
                            passOverrides->second.find(packetWrite.binding);
                        bindingOverride != passOverrides->second.end()) {
                        resourceName = bindingOverride->second.resourceName;
                        previousFrame = bindingOverride->second.previousFrame;
                    }
                }
                const auto image = resolve(
                    resourceName,
                    frameIndex,
                    previousFrame);
                if (!image) {
                    throw std::invalid_argument(
                        "Frame descriptor references an unresolved texture");
                }
                transition(
                    commandList,
                    *image,
                    packetWrite.type ==
                            Tasrovy::Render::FrameDescriptorType::StorageImage
                        ? RenderResourceState::StorageWrite
                        : RenderResourceState::ShaderRead);
                if (packetWrite.type ==
                    Tasrovy::Render::FrameDescriptorType::StorageImage) {
                    write.type = DescriptorResourceType::StorageImage;
                    write.imageInfo = image->getDescriptorInfoForStorage();
                } else {
                    write.type = DescriptorResourceType::CombinedImageSampler;
                    write.image = image;
                }
                break;
            }
            case Tasrovy::Render::FrameDescriptorSource::MaterialTexture: {
                const auto material = bindings.materialTextures.find(materialId);
                if (material == bindings.materialTextures.end()) {
                    throw std::invalid_argument(
                        "Frame draw references missing material bindings");
                }
                const auto texture = material->second.find(
                    packetWrite.materialSlot);
                if (texture == material->second.end() || !texture->second) {
                    throw std::invalid_argument(
                        "Frame draw references a missing material texture");
                }
                write.type = DescriptorResourceType::CombinedImageSampler;
                write.image = texture->second;
                break;
            }
            case Tasrovy::Render::FrameDescriptorSource::ImportedResource: {
                const auto imported = bindings.importedImages.find(
                    packetWrite.importedResource);
                if (imported == bindings.importedImages.end()) {
                    throw std::invalid_argument(
                        "Frame descriptor references an unresolved imported resource");
                }
                write.type = DescriptorResourceType::CombinedImageSampler;
                if (imported->second.useImageInfo) {
                    write.imageInfo = imported->second.imageInfo;
                } else {
                    write.image = imported->second.image;
                }
                break;
            }
            case Tasrovy::Render::FrameDescriptorSource::RenderBuffer:
                write.type = DescriptorResourceType::StorageBuffer;
                write.buffer = resolveBuffer(packetWrite.resourceId);
                if (!write.buffer) {
                    throw std::invalid_argument(
                        "Frame descriptor references an unresolved buffer");
                }
                break;
            }
            if ((write.type == DescriptorResourceType::UniformBuffer ||
                 write.type == DescriptorResourceType::StorageBuffer) &&
                !write.buffer) {
                throw std::invalid_argument(
                    "Frame descriptor references an unresolved GPUScene buffer");
            }
            writes.push_back(std::move(write));
        }
        device.updateDescriptorSet(
            compiled.descriptorSets[descriptorIndex], writes);
    };

    for (const auto& passPlan : plan.passes) {
        auto* compiled = compiledPipeline_.find(passPlan.passId);
        if (!compiled || !compiled->framePass ||
            frameIndex >= compiled->rhiPasses.size() ||
            !compiled->rhiPasses[frameIndex]) {
            throw std::invalid_argument(
                "Execution plan references an uncompiled pass");
        }
        auto& packetPass = *compiled->framePass;
        auto& rhiPass = *compiled->rhiPasses[frameIndex];
        const auto& passDesc = rhiPass.getDesc();

        executePreBarriers(commandList, passPlan, frameIndex);
        const bool hasRasterCommands = std::any_of(
            packetPass.commands.begin(), packetPass.commands.end(),
            [](const Tasrovy::Render::FrameCommandPacket& command) {
                using Type = Tasrovy::Render::FrameCommandType;
                return command.type == Type::Draw ||
                    command.type == Type::DrawIndexed ||
                    command.type == Type::DrawSkybox;
            });

        if (!hasRasterCommands && swapchainPassOpen && context.swapchainTarget) {
            commandList.endSwapchainRenderPass(*context.swapchainTarget);
            if (drawOverlay && context.overlay) {
                commandList.renderOverlay(
                    *context.overlay, *context.swapchainTarget);
                drawOverlay = false;
            }
            swapchainPassOpen = false;
        }

        commandList.setViewport(
            0.0f, 0.0f,
            static_cast<float>(passDesc.width),
            static_cast<float>(passDesc.height));
        commandList.setScissor(0, 0, passDesc.width, passDesc.height);
        if (packetPass.virtualShadowPage) {
            const auto& page = *packetPass.virtualShadowPage;
            commandList.setVirtualShadowPage({
                page.pageX, page.pageY, page.pageSize,
                passDesc.width, passDesc.height
            });
        }

        if (hasRasterCommands && compiled->usesSwapchain) {
            if (!context.swapchainTarget) {
                throw std::invalid_argument(
                    "Swapchain pass has no acquired render target");
            }
            if (!swapchainPassOpen) {
                commandList.beginSwapchainRenderPass(*context.swapchainTarget);
                swapchainPassOpen = true;
                result.swapchainUsed = true;
            }
        } else if (hasRasterCommands) {
            if (swapchainPassOpen && context.swapchainTarget) {
                commandList.endSwapchainRenderPass(*context.swapchainTarget);
                if (drawOverlay && context.overlay) {
                    commandList.renderOverlay(
                        *context.overlay, *context.swapchainTarget);
                    drawOverlay = false;
                }
                swapchainPassOpen = false;
            }
            commandList.beginRenderPass(rhiPass);
        }

        std::shared_ptr<Pipeline> pipeline = compiled->gpuPipeline;
        if (const auto permutation = compiled->permutations.find(
                packetPass.selectedPermutationKey);
            permutation != compiled->permutations.end()) {
            pipeline = permutation->second;
        }
        if (pipeline) {
            commandList.bindPipeline(
                pipeline->getNativePipeline(), pipeline->getNativeLayout(),
                packetPass.execution ==
                        Tasrovy::Render::PipelinePassExecution::Compute
                    ? 1u
                    : 0u);
        }

        const bool recordTimestamp = context.timestampQueryPool != 0 &&
            result.timestampQueryCount + 1u <
                context.timestampQueryCapacity;
        if (recordTimestamp) {
            commandList.writeTimestamp(
                context.timestampQueryPool,
                result.timestampQueryCount++, true);
            result.timestampPassNames.push_back(packetPass.name);
        }

        for (const auto& command : packetPass.commands) {
            using Type = Tasrovy::Render::FrameCommandType;
            switch (command.type) {
            case Type::Draw:
                if (!pipeline) break;
                if (!packetPass.parameters.uniformData.empty() &&
                    frameIndex < compiled->uniformBuffers.size()) {
                    compiled->uniformBuffers[frameIndex]->setData(
                        packetPass.parameters.uniformData.data(),
                        packetPass.parameters.uniformData.size());
                }
                descriptorWrites(*compiled, packetPass, frameIndex, 0);
                if (frameIndex < compiled->descriptorSets.size()) {
                    commandList.bindDescriptorSet(
                        0, compiled->descriptorSets[frameIndex]);
                }
                commandList.draw(command.vertexCount, command.instanceCount);
                break;
            case Type::DrawSkybox:
                if (!pipeline || !bindings.skyboxVertexBuffer ||
                    !bindings.skyboxIndexBuffer ||
                    bindings.skyboxIndexCount == 0) break;
                if (!packetPass.parameters.uniformData.empty() &&
                    frameIndex < compiled->uniformBuffers.size()) {
                    compiled->uniformBuffers[frameIndex]->setData(
                        packetPass.parameters.uniformData.data(),
                        packetPass.parameters.uniformData.size());
                }
                descriptorWrites(*compiled, packetPass, frameIndex, 0);
                if (frameIndex < compiled->descriptorSets.size()) {
                    commandList.bindDescriptorSet(
                        0, compiled->descriptorSets[frameIndex]);
                }
                commandList.bindVertexBuffer(*bindings.skyboxVertexBuffer);
                commandList.bindIndexBuffer(*bindings.skyboxIndexBuffer);
                commandList.drawIndexed(bindings.skyboxIndexCount);
                break;
            case Type::DrawIndexed: {
                if (!pipeline || command.drawIndex >= packetPass.draws.size()) {
                    break;
                }
                const auto& draw = packetPass.draws[command.drawIndex];
                const auto mesh = bindings.meshes.find(draw.meshId);
                if (mesh == bindings.meshes.end() ||
                    !mesh->second.vertexBuffer || !mesh->second.indexBuffer) {
                    throw std::invalid_argument(
                        "Indexed draw references an unbound mesh");
                }
                const uint32_t descriptorIndex =
                    frameIndex * compiled->descriptorSetsPerFrame +
                    command.drawIndex;
                if (!packetPass.parameters.uniformData.empty() &&
                    frameIndex < compiled->uniformBuffers.size()) {
                    compiled->uniformBuffers[frameIndex]->setData(
                        packetPass.parameters.uniformData.data(),
                        packetPass.parameters.uniformData.size());
                }
                descriptorWrites(
                    *compiled, packetPass, descriptorIndex,
                    draw.materialIndex);
                commandList.bindVertexBuffer(*mesh->second.vertexBuffer);
                commandList.bindIndexBuffer(*mesh->second.indexBuffer);
                commandList.setFrontFace(
                    draw.flipProjectionY
                        ? FrontFaceClockwise
                        : FrontFaceCounterClockwise);
                if (descriptorIndex < compiled->descriptorSets.size()) {
                    commandList.bindDescriptorSet(
                        0, compiled->descriptorSets[descriptorIndex]);
                }
                commandList.drawIndexed(
                    command.indexCount, command.instanceCount,
                    command.firstIndex, command.vertexOffset,
                    command.firstInstance);
                break;
            }
            case Type::Dispatch:
                if (!pipeline) break;
                descriptorWrites(*compiled, packetPass, frameIndex, 0);
                if (frameIndex < compiled->descriptorSets.size()) {
                    commandList.bindDescriptorSet(
                        0, compiled->descriptorSets[frameIndex]);
                }
                commandList.dispatch(
                    command.groupCountX,
                    command.groupCountY,
                    command.groupCountZ);
                break;
            case Type::CopyBuffer: {
                const auto source = resolveBuffer(command.sourceResourceId);
                const auto destination = resolveBuffer(
                    command.destinationResourceId);
                if (!source || !destination) {
                    throw std::invalid_argument(
                        "Copy command references an unresolved buffer");
                }
                commandList.copyBuffer(
                    *source, *destination, command.byteSize);
                break;
            }
            }
        }

        if (hasRasterCommands && !compiled->usesSwapchain) {
            commandList.endRenderPass();
        }
        executePostBarriers(commandList, passPlan, frameIndex);
        if (recordTimestamp) {
            commandList.writeTimestamp(
                context.timestampQueryPool,
                result.timestampQueryCount++, false);
        }
    }

    if (swapchainPassOpen && context.swapchainTarget) {
        commandList.endSwapchainRenderPass(*context.swapchainTarget);
        if (drawOverlay && context.overlay) {
            commandList.renderOverlay(*context.overlay, *context.swapchainTarget);
        }
    }
    return result;
}

void VulkanFrameExecutor::rebuildDisplayResources(
    Device& device,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config) {
    if (!plan.valid()) {
        reset();
        throw std::invalid_argument(
            "VulkanFrameExecutor cannot rebuild an invalid execution plan");
    }
    for (const auto& resource : plan.resources) {
        if (!isDisplayResource(resource.description)) {
            continue;
        }
        const auto found = textures_.find(resource.resourceName);
        if (found == textures_.end()) {
            continue;
        }
        for (const auto& image : found->second) {
            resourceStates_.erase(image.get());
            const auto bytes = imageBytes_.find(image.get());
            if (bytes != imageBytes_.end()) {
                allocatedBytes_ -= bytes->second;
                imageBytes_.erase(bytes);
            }
        }
        textures_.erase(found);
        textureInfos_.erase(resource.resourceName);
    }
    allocateResources(device, plan, config, true);

    std::unordered_set<std::string> displayResourceNames;
    for (const auto& resource : plan.resources) {
        if (isDisplayResource(resource.description)) {
            displayResourceNames.emplace(resource.resourceName);
        }
    }
    for (const auto& passPlan : plan.passes) {
        if (!passUsesResourceIn(passPlan, displayResourceNames)) {
            continue;
        }
        auto* compiled = compiledPipeline_.find(passPlan.passId);
        if (!compiled) {
            continue;
        }
        compiled->rhiPasses.clear();
        compiled->rhiPasses.reserve(std::max(config.framesInFlight, 1u));
        for (uint32_t frame = 0;
             frame < std::max(config.framesInFlight, 1u);
             ++frame) {
            PassDesc passDesc;
            passDesc.name = passPlan.name;
            passDesc.width = config.displayWidth;
            passDesc.height = config.displayHeight;
            for (const auto& attachment : passPlan.colorAttachments) {
                if (const auto found = textureInfos_.find(
                        attachment.resourceName);
                    found != textureInfos_.end()) {
                    passDesc.width = found->second.width;
                    passDesc.height = found->second.height;
                }
                passDesc.colorAttachments.push_back({
                    attachment.resourceName,
                    resolve(attachment.resourceName, frame),
                    toAttachmentLoad(attachment.load),
                    toAttachmentStore(attachment.store),
                    false,
                    Tasrovy::Base::TSVec4f(
                        passPlan.clearColor[0], passPlan.clearColor[1],
                        passPlan.clearColor[2], passPlan.clearColor[3])
                });
            }
            if (passPlan.depthAttachment) {
                const auto& attachment = *passPlan.depthAttachment;
                if (const auto found = textureInfos_.find(
                        attachment.resourceName);
                    found != textureInfos_.end()) {
                    passDesc.width = found->second.width;
                    passDesc.height = found->second.height;
                }
                passDesc.depthAttachment = RHIAttachmentDesc{
                    attachment.resourceName,
                    resolve(attachment.resourceName, frame),
                    toAttachmentLoad(attachment.load),
                    toAttachmentStore(attachment.store),
                    attachment.readOnly,
                    Tasrovy::Base::TSVec4f(0.0f),
                    attachment.clearDepth
                };
            }
            compiled->rhiPasses.push_back(device.retainResource(
                config.displayScope,
                device.createPass(std::move(passDesc))));
        }
    }
}

void VulkanFrameExecutor::allocateResources(
    Device& device,
    const RenderFrameExecutionPlan& plan,
    const FrameResourceConfig& config,
    bool displayOnly) {
    framesInFlight_ = std::max(config.framesInFlight, 1u);
    std::unordered_map<int32_t, TextureFrames> transientPool;
    for (const auto& resource : plan.resources) {
        const auto& description = resource.description;
        const bool displayResource = isDisplayResource(description);
        if (displayOnly != displayResource) {
            continue;
        }

        auto& frames = textures_[resource.resourceName];
        frames.resize(framesInFlight_);

        const RenderTextureDesc textureDesc{
            description.name,
            resolveExtent(
                description.extent,
                description.width,
                description.widthScale,
                config.displayWidth,
                config.internalWidth),
            resolveExtent(
                description.extent,
                description.height,
                description.heightScale,
                config.displayHeight,
                config.internalHeight),
            toRHIFormat(description.format),
            description.external,
            resource.storage
        };
        textureInfos_[resource.resourceName] = {
            textureDesc.width,
            textureDesc.height,
            textureDesc.format,
            textureDesc.external
        };
        if (resource.external) {
            continue;
        }

        const auto pooled =
            transientPool.find(resource.allocationSlot);
        if (resource.allocationSlot >= 0 &&
            pooled != transientPool.end()) {
            frames = pooled->second;
            continue;
        }

        for (uint32_t frame = 0; frame < framesInFlight_; ++frame) {
            std::shared_ptr<Image> image;
            if (description.name == "VirtualShadowAtlas") {
                image = device.createVirtualShadowMap({
                    description.name,
                    textureDesc.width,
                    config.virtualShadowPageSize,
                    config.virtualShadowPageCount,
                    device.resolveRenderTextureFormat(textureDesc.format)
                });
            } else {
                image = device.createRenderTexture(textureDesc);
            }

            const auto scope = displayResource
                ? config.displayScope
                : config.sceneScope;
            frames[frame] =
                device.retainResource(scope, std::move(image));
            if (frames[frame]) {
                const uint64_t imageBytes =
                    static_cast<uint64_t>(textureDesc.width) *
                    static_cast<uint64_t>(textureDesc.height) *
                    bytesPerPixel(textureDesc.format);
                const auto [_, inserted] =
                    imageBytes_.emplace(frames[frame].get(), imageBytes);
                if (inserted) {
                    allocatedBytes_ += imageBytes;
                }
            }
        }
        if (resource.allocationSlot >= 0) {
            transientPool.emplace(resource.allocationSlot, frames);
        }
    }
}

void VulkanFrameExecutor::executePreBarriers(
    CommandList& commandList,
    const RenderPassExecutionPlan& pass,
    uint32_t frameIndex) {
    executeBarriers(commandList, pass.preTransitions, frameIndex);
}

void VulkanFrameExecutor::executePostBarriers(
    CommandList& commandList,
    const RenderPassExecutionPlan& pass,
    uint32_t frameIndex) {
    executeBarriers(commandList, pass.postTransitions, frameIndex);
}

void VulkanFrameExecutor::executeBarriers(
    CommandList& commandList,
    const std::vector<RenderResourceTransition>& transitions,
    uint32_t frameIndex) {
    for (const auto& barrier : transitions) {
        if (barrier.kind == RenderResourceKind::Buffer) {
            const auto buffer = resolveBuffer(barrier.resourceId);
            if (!buffer) {
                throw std::invalid_argument(
                    "Frame barrier references an unresolved buffer");
            }
            const auto state = [](RenderResourceState value) {
                struct BufferState {
                    PipelineStage stage;
                    ResourceAccess access;
                };
                switch (value) {
                case RenderResourceState::TransferRead:
                    return BufferState{
                        PipelineStage::Transfer,
                        ResourceAccess::TransferRead};
                case RenderResourceState::TransferWrite:
                    return BufferState{
                        PipelineStage::Transfer,
                        ResourceAccess::TransferWrite};
                case RenderResourceState::StorageRead:
                    return BufferState{
                        PipelineStage::VertexShader |
                            PipelineStage::FragmentShader |
                            PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead};
                case RenderResourceState::StorageWrite:
                    return BufferState{
                        PipelineStage::VertexShader |
                            PipelineStage::FragmentShader |
                            PipelineStage::ComputeShader,
                        ResourceAccess::ShaderRead |
                            ResourceAccess::ShaderWrite};
                case RenderResourceState::HostWrite:
                    return BufferState{
                        PipelineStage::Host,
                        ResourceAccess::HostWrite};
                case RenderResourceState::Undefined:
                    return BufferState{
                        PipelineStage::TopOfPipe,
                        ResourceAccess::None};
                default:
                    return BufferState{
                        PipelineStage::AllCommands,
                        ResourceAccess::MemoryRead |
                            ResourceAccess::MemoryWrite};
                }
            };
            const auto before = state(barrier.before);
            const auto after = state(barrier.after);
            commandList.bufferMemoryBarrier(
                *buffer,
                before.stage,
                after.stage,
                before.access,
                after.access);
            continue;
        }
        const auto image = resolve(
            barrier.resourceName,
            frameIndex,
            barrier.previousFrame);
        if (!image) {
            continue;
        }
        transition(
            commandList,
            *image,
            barrier.after,
            barrier.forceMemoryBarrier);
    }
}

void VulkanFrameExecutor::transition(
    CommandList& commandList,
    Image& image,
    RenderResourceState desired,
    bool forceMemoryBarrier) {
    const auto found = resourceStates_.find(&image);
    const RenderResourceState current =
        found == resourceStates_.end()
        ? RenderResourceState::Undefined
        : found->second;
    if (current == desired && !forceMemoryBarrier) {
        return;
    }
    const auto source = translateResourceState(current);
    const auto destination = translateResourceState(desired);

    const auto format = static_cast<VkFormat>(image.getFormat());
    const bool depth =
        format == VK_FORMAT_D32_SFLOAT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT;
    const bool stencil =
        format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = source.access;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = source.layout;
    barrier.newLayout = destination.layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = reinterpret_cast<VkImage>(image.getNativeImage());
    barrier.subresourceRange.aspectMask = depth
        ? VK_IMAGE_ASPECT_DEPTH_BIT |
            (stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u)
        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.getMipLevels();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        reinterpret_cast<VkCommandBuffer>(
            commandList.getNativeCommandBuffer()),
        source.stages,
        destination.stages,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
    resourceStates_[&image] = desired;
}

std::shared_ptr<Image> VulkanFrameExecutor::resolve(
    const std::string& resourceName,
    uint32_t frameIndex,
    bool previousFrame) const {
    const auto found = textures_.find(resourceName);
    if (found == textures_.end() || found->second.empty()) {
        return nullptr;
    }
    uint32_t resolvedFrame = frameIndex % found->second.size();
    if (previousFrame) {
        resolvedFrame =
            (resolvedFrame + static_cast<uint32_t>(found->second.size()) - 1u) %
            static_cast<uint32_t>(found->second.size());
    }
    return found->second[resolvedFrame];
}

std::shared_ptr<Buffer> VulkanFrameExecutor::resolveBuffer(
    uint64_t resourceId) const {
    const auto found = buffers_.find(resourceId);
    return found == buffers_.end() ? nullptr : found->second;
}

const ResolvedTextureInfo* VulkanFrameExecutor::textureInfo(
    const std::string& resourceName) const {
    const auto found = textureInfos_.find(resourceName);
    return found == textureInfos_.end() ? nullptr : &found->second;
}

const VulkanFrameExecutor::TextureMap&
VulkanFrameExecutor::textures() const {
    return textures_;
}

uint64_t VulkanFrameExecutor::allocatedBytes() const {
    return allocatedBytes_;
}

CompiledRenderPipeline& VulkanFrameExecutor::compiledPipeline() {
    return compiledPipeline_;
}

const CompiledRenderPipeline& VulkanFrameExecutor::compiledPipeline() const {
    return compiledPipeline_;
}

} // namespace Tasrovy::RHI::Vulkan
