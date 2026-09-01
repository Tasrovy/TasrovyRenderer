#include "FrameCompiler.h"

#include "Camera.h"
#include "Material.h"
#include "Mesh.h"
#include "Object.h"
#include "Pipeline.h"
#include "PipelinePass.h"
#include "RenderGraph.h"
#include "Scene.h"
#include "Shader.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Tasrovy::Render {
namespace {

uint64_t hashResourceName(const std::string& name) {
    uint64_t value = 1469598103934665603ull;
    for (const unsigned char character : name) {
        value ^= character;
        value *= 1099511628211ull;
    }
    return value == 0 ? 1 : value;
}

FrameShaderPacket shaderPacket(const std::shared_ptr<Shader>& shader) {
    if (!shader) {
        return {};
    }
    return {
        shader->getSourcePath(),
        shader->getEntry(),
        shader->getType(),
        shader->getPermutation()
    };
}

FrameResourceUse resourceUse(const PipelineResourceRef& resource) {
    return {
        hashResourceName(resource.resource),
        resource.slot,
        resource.resource,
        resource.binding,
        resource.access,
        resource.previousFrame
    };
}

} // namespace

uint64_t FrameCompiler::idFor(const void* object) {
    if (!object) {
        return 0;
    }
    const auto [found, inserted] = objectIds_.try_emplace(object, nextId_);
    if (inserted) {
        ++nextId_;
    }
    return found->second;
}

uint32_t FrameCompiler::objectIndexFor(
    RenderObjectId objectId, uint32_t submeshIndex) {
    auto& submeshes = objectIndices_[objectId];
    const auto [found, inserted] = submeshes.try_emplace(
        submeshIndex, nextObjectIndex_);
    if (inserted) ++nextObjectIndex_;
    return found->second;
}

uint32_t FrameCompiler::materialIndexFor(RenderMaterialId materialId) {
    const auto [found, inserted] = materialIndices_.try_emplace(
        materialId, nextMaterialIndex_);
    if (inserted) ++nextMaterialIndex_;
    return found->second;
}

FramePacket FrameCompiler::compile(
    const Scene& scene,
    const PipelineBase& pipeline,
    const RenderGraph& renderGraph,
    uint64_t frameNumber) {
    FramePacket frame;
    sourceRegistry_.clear();
    frame.frameNumber = frameNumber;
    frame.pipelineName = pipeline.getName();
    frame.diagnostics = renderGraph.getDiagnostics();

    if (!renderGraph.isValid()) {
        if (frame.diagnostics.empty()) {
            frame.diagnostics.emplace_back(
                "FrameCompiler rejected an invalid Render Graph");
        }
        return frame;
    }

    if (const auto* camera = scene.getPrimaryCamera()) {
        frame.camera.valid = true;
        frame.camera.view = camera->getViewMatrix();
        frame.camera.projection = camera->getProjectionMatrix();
        frame.camera.position = camera->getPosition();
        frame.camera.nearPlane = camera->getNearPlane();
        frame.camera.farPlane = camera->getFarPlane();
    } else {
        frame.diagnostics.emplace_back(
            "FrameCompiler requires a primary camera");
    }

    frame.textures.reserve(pipeline.getTextures().size());
    for (const auto& texture : pipeline.getTextures()) {
        frame.textures.push_back({
            hashResourceName(texture.name),
            texture
        });
    }
    frame.buffers.reserve(pipeline.getBuffers().size());
    for (const auto& buffer : pipeline.getBuffers()) {
        if (buffer.name.empty() || buffer.byteSize == 0) {
            frame.diagnostics.emplace_back(
                "FrameCompiler rejected an invalid pipeline buffer");
            continue;
        }
        frame.buffers.push_back({hashResourceName(buffer.name), buffer});
    }

    std::unordered_set<RenderMeshId> emittedMeshes;
    std::unordered_set<RenderMaterialId> emittedMaterials;
    frame.passes.reserve(renderGraph.getNodes().size());
    for (const auto& node : renderGraph.getNodes()) {
        const auto& pass = node.pass;
        if (!pass) {
            frame.diagnostics.emplace_back(
                "FrameCompiler encountered a null pipeline pass");
            continue;
        }

        FramePassPacket packet;
        packet.id = idFor(pass.get());
        packet.name = pass->getName();
        packet.type = pass->getType();
        packet.execution = pass->getExecution();
        packet.parameterProvider = pass->getParameterProvider();
        packet.viewIndex = pass->getViewIndex();
        packet.state.topology = pass->getTopology();
        packet.state.cullMode = pass->getCullMode();
        packet.state.depthTest = pass->getDepthTest();
        packet.state.depthWrite = pass->getDepthWrite();
        packet.state.depthTestMode = pass->getDepthTestMode();
        packet.state.blendMode = pass->getBlendMode();
        packet.state.clearColor = pass->getClearColor();
        packet.vertexShader = shaderPacket(pass->getVertexShader());
        packet.fragmentShader = shaderPacket(pass->getFragmentShader());
        packet.computeShader = shaderPacket(pass->getComputeShader());
        packet.selectedPermutationKey = pass->getSelectedPermutationKey();
        for (const auto& attachment : pass->getColorAttachments()) {
            packet.colorAttachments.push_back({
                hashResourceName(attachment.resource),
                attachment.resource,
                attachment.load,
                attachment.store,
                false,
                1.0f
            });
        }
        if (const auto* depth = pass->getDepthAttachment()) {
            packet.depthAttachment = FrameAttachmentPacket{
                hashResourceName(depth->resource),
                depth->resource,
                depth->load,
                depth->store,
                depth->readOnly,
                depth->clearDepth
            };
        }
        packet.materialTextures = pass->getMaterialTextures();
        for (const auto& objectReference : pass->getObjects()) {
            if (const auto object = objectReference.lock()) {
                const auto objectId = object->getRenderId();
                packet.objectIds.push_back(objectId);
                sourceRegistry_.objects[objectId] = object;
            }
        }
        packet.sampledTextures = pass->getSampledTextures();
        for (const auto& permutation : pass->getShaderPermutations()) {
            packet.permutations.push_back({
                permutation.key,
                shaderPacket(permutation.vertexShader),
                shaderPacket(permutation.fragmentShader),
                shaderPacket(permutation.computeShader)
            });
        }
        if (const auto* virtualPage = pass->getVirtualShadowPage()) {
            packet.virtualShadowPage = *virtualPage;
        }

        for (const auto& read : pass->getReadResources()) {
            packet.reads.push_back(resourceUse(read));
        }
        for (const auto& write : pass->getWriteResources()) {
            packet.writes.push_back(resourceUse(write));
        }

        const auto toVertexFormat = [](PipelineVertexFormat format) {
            switch (format) {
            case PipelineVertexFormat::Float2: return FrameVertexFormat::Float2;
            case PipelineVertexFormat::Float3: return FrameVertexFormat::Float3;
            case PipelineVertexFormat::Float4: return FrameVertexFormat::Float4;
            }
            return FrameVertexFormat::Float3;
        };
        packet.vertexLayout.stride = pass->getVertexLayout().stride;
        for (const auto& attribute : pass->getVertexLayout().attributes) {
            packet.vertexLayout.attributes.push_back({
                attribute.location,
                toVertexFormat(attribute.format),
                attribute.offset
            });
        }

        const bool computePass =
            pass->getExecution() == PipelinePassExecution::Compute;
        if (pass->getUniformByteSize() != 0) {
            packet.descriptorLayout.bindings.push_back({
                0,
                FrameDescriptorType::UniformBuffer,
                pass->getUniformShaderStages(),
                0,
                false
            });
            packet.parameters.uniformByteSize = pass->getUniformByteSize();
            packet.descriptorWrites.push_back({
                0,
                FrameDescriptorType::UniformBuffer,
                FrameDescriptorSource::UniformData
            });
        }
        const auto addGpuSceneBinding = [&](uint32_t binding,
            FrameDescriptorType type, FrameDescriptorSource source) {
            packet.descriptorLayout.bindings.push_back({
                binding, type,
                FrameShaderStageVertex | FrameShaderStageFragment |
                    FrameShaderStageCompute,
                0, false});
            packet.descriptorWrites.push_back({binding, type, source});
        };
        addGpuSceneBinding(
            FrameViewUniformBinding, FrameDescriptorType::UniformBuffer,
            FrameDescriptorSource::ViewUniform);
        addGpuSceneBinding(
            FrameObjectDataBinding, FrameDescriptorType::StorageBuffer,
            FrameDescriptorSource::ObjectData);
        addGpuSceneBinding(
            FrameMaterialDataBinding, FrameDescriptorType::StorageBuffer,
            FrameDescriptorSource::MaterialData);
        addGpuSceneBinding(
            FrameSceneLightBinding, FrameDescriptorType::StorageBuffer,
            FrameDescriptorSource::SceneLights);
        for (const auto& read : packet.reads) {
            if (read.access ==
                    PipelineResourceAccess::BufferTransferRead ||
                read.access == PipelineResourceAccess::ColorRead ||
                read.access == PipelineResourceAccess::DepthRead) {
                // Attachment load/read-only depth uses are represented in the
                // execution plan and dynamic-rendering attachments. They are
                // ordering/lifetime dependencies, not shader descriptors.
                continue;
            }
            if (read.binding == 0 && pass->getUniformByteSize() != 0) {
                continue;
            }
            const FrameDescriptorBinding binding{
                read.binding,
                read.access == PipelineResourceAccess::StorageRead
                    ? FrameDescriptorType::StorageImage
                    : read.access ==
                            PipelineResourceAccess::BufferStorageRead
                        ? FrameDescriptorType::StorageBuffer
                    : FrameDescriptorType::CombinedImageSampler,
                computePass
                    ? FrameShaderStageCompute
                    : FrameShaderStageFragment,
                read.id,
                read.previousFrame
            };
            packet.descriptorLayout.bindings.push_back(binding);
            packet.parameters.resourceBindings.push_back(binding);
            packet.descriptorWrites.push_back({
                binding.binding,
                binding.type,
                read.access == PipelineResourceAccess::BufferStorageRead
                    ? FrameDescriptorSource::RenderBuffer
                    : FrameDescriptorSource::RenderTexture,
                read.id,
                0,
                read.resourceName,
                {},
                {},
                read.previousFrame
            });
        }
        const auto addDescriptorBinding = [&packet](
            FrameDescriptorBinding binding) {
            const auto existing = std::find_if(
                packet.descriptorLayout.bindings.begin(),
                packet.descriptorLayout.bindings.end(),
                [&](const auto& value) {
                    return value.binding == binding.binding;
                });
            if (existing == packet.descriptorLayout.bindings.end()) {
                packet.descriptorLayout.bindings.push_back(binding);
                packet.parameters.resourceBindings.push_back(binding);
            }
        };
        for (const auto& requirement : pass->getMaterialTextures()) {
            addDescriptorBinding({
                requirement.binding,
                FrameDescriptorType::CombinedImageSampler,
                FrameShaderStageFragment,
                0,
                false
            });
        }
        for (const auto& external : pass->getImportedTextures()) {
            addDescriptorBinding({
                external.binding,
                FrameDescriptorType::CombinedImageSampler,
                external.shaderStages,
                0,
                false
            });
        }
        for (const auto& write : packet.writes) {
            if (write.access == PipelineResourceAccess::StorageWrite) {
                addDescriptorBinding({
                    write.binding,
                    FrameDescriptorType::StorageImage,
                    FrameShaderStageCompute,
                    write.id,
                    false
                });
                packet.descriptorWrites.push_back({
                    write.binding,
                    FrameDescriptorType::StorageImage,
                    FrameDescriptorSource::RenderTexture,
                    write.id,
                    0,
                    write.resourceName,
                    {},
                    {},
                    false
                });
            } else if (write.access ==
                       PipelineResourceAccess::BufferStorageWrite) {
                addDescriptorBinding({
                    write.binding,
                    FrameDescriptorType::StorageBuffer,
                    FrameShaderStageCompute,
                    write.id,
                    false
                });
                packet.descriptorWrites.push_back({
                    write.binding,
                    FrameDescriptorType::StorageBuffer,
                    FrameDescriptorSource::RenderBuffer,
                    write.id,
                    0,
                    write.resourceName,
                    {},
                    {},
                    false
                });
            }
        }

        for (const auto& external : pass->getImportedTextures()) {
            packet.descriptorWrites.push_back({
                external.binding,
                FrameDescriptorType::CombinedImageSampler,
                FrameDescriptorSource::ImportedResource,
                0,
                0,
                {},
                {},
                external.handle
            });
        }
        for (const auto& requirement : pass->getMaterialTextures()) {
            packet.descriptorWrites.push_back({
                requirement.binding,
                FrameDescriptorType::CombinedImageSampler,
                FrameDescriptorSource::MaterialTexture,
                0,
                0,
                {},
                requirement.slot
            });
        }
        std::sort(
            packet.descriptorLayout.bindings.begin(),
            packet.descriptorLayout.bindings.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.binding < rhs.binding;
            });
        const auto normalizeBindings = [&](auto& bindings) {
            for (size_t index = 1; index < bindings.size();) {
                auto& previous = bindings[index - 1];
                const auto& current = bindings[index];
                if (previous.binding != current.binding) {
                    ++index;
                    continue;
                }
                if (previous.type != current.type ||
                    previous.resourceId != current.resourceId ||
                    previous.previousFrame != current.previousFrame) {
                    frame.diagnostics.push_back(
                        "Pass '" + packet.name +
                        "' declares incompatible descriptor sources at binding " +
                        std::to_string(current.binding));
                }
                previous.stages |= current.stages;
                bindings.erase(bindings.begin() +
                    static_cast<std::ptrdiff_t>(index));
            }
        };
        normalizeBindings(packet.descriptorLayout.bindings);
        std::sort(
            packet.parameters.resourceBindings.begin(),
            packet.parameters.resourceBindings.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.binding < rhs.binding;
            });
        normalizeBindings(packet.parameters.resourceBindings);
        for (const auto& objectReference : pass->getObjects()) {
            const auto object = objectReference.lock();
            const auto mesh = object ? object->getMesh() : nullptr;
            if (!object || !object->isActive() || !mesh) {
                continue;
            }

            const auto objectId = object->getRenderId();
            const auto meshId = idFor(mesh.get());
            if (emittedMeshes.insert(meshId).second) {
                frame.meshes.push_back({meshId});
                sourceRegistry_.meshes[meshId] = mesh;
            }

            const auto emitDraw = [&](
                uint32_t submeshIndex,
                uint32_t firstIndex,
                uint32_t indexCount,
                const std::shared_ptr<Material>& material) {
                const auto materialId = idFor(material.get());
                const auto materialIndex = materialIndexFor(materialId);
                const auto objectIndex = objectIndexFor(
                    objectId, submeshIndex);
                sourceRegistry_.objectDataSources[objectIndex] = {
                    object, material, submeshIndex};
                if (material &&
                    emittedMaterials.insert(materialId).second) {
                    frame.materials.push_back({materialId, materialIndex});
                    sourceRegistry_.materials[materialId] = material;
                    sourceRegistry_.materialIndices[materialId] =
                        materialIndex;
                }
                packet.draws.push_back({
                    meshId,
                    objectIndex,
                    materialIndex,
                    submeshIndex,
                    firstIndex,
                    indexCount,
                    object->getFlipProjectionY()
                });
                auto& emittedDraw = packet.draws.back();
                emittedDraw.descriptorWrites = packet.descriptorWrites;
                for (auto& descriptorWrite : emittedDraw.descriptorWrites) {
                    if (descriptorWrite.source ==
                        FrameDescriptorSource::MaterialTexture) {
                        descriptorWrite.materialId = materialId;
                    }
                }
                packet.commands.push_back({
                    FrameCommandType::DrawIndexed,
                    static_cast<uint32_t>(packet.draws.size() - 1),
                    0,
                    indexCount,
                    1,
                    firstIndex,
                    0,
                    0,
                    objectIndex
                });
            };

            const auto& submeshes = mesh->getSubmeshes();
            if (submeshes.empty()) {
                emitDraw(
                    0,
                    0,
                    static_cast<uint32_t>(mesh->getIndexCount()),
                    object->getMaterial());
            } else {
                for (uint32_t index = 0;
                     index < static_cast<uint32_t>(submeshes.size());
                     ++index) {
                    const auto& submesh = submeshes[index];
                    emitDraw(
                        index,
                        submesh.getIndexOffset(),
                        submesh.getIndexCount(),
                        object->getSubmeshMaterial(index));
                }
            }
        }

        packet.descriptorLayout.setsPerFrame = std::max(
            1u,
            pass->getExecution() == PipelinePassExecution::Mesh
                ? static_cast<uint32_t>(packet.draws.size())
                : 1u);

        if (pass->getExecution() == PipelinePassExecution::Fullscreen) {
            packet.commands.push_back({
                FrameCommandType::Draw, 0, 3, 0, 1
            });
        } else if (pass->getExecution() == PipelinePassExecution::Skybox) {
            packet.commands.push_back({FrameCommandType::DrawSkybox});
        } else if (pass->getExecution() == PipelinePassExecution::Compute) {
            const auto* dispatch = pass->getDispatch();
            if (!dispatch) {
                frame.diagnostics.push_back(
                    "Compute pass '" + packet.name +
                    "' has no dispatch dimensions");
            } else {
                FrameCommandPacket command;
                command.type = FrameCommandType::Dispatch;
                command.groupCountX = dispatch->groupCountX;
                command.groupCountY = dispatch->groupCountY;
                command.groupCountZ = dispatch->groupCountZ;
                packet.commands.push_back(command);
            }
        }
        for (const auto& copy : pass->getCopyCommands()) {
            FrameCommandPacket command;
            command.type = FrameCommandType::CopyBuffer;
            command.sourceResourceId = hashResourceName(copy.source);
            command.destinationResourceId = hashResourceName(copy.destination);
            command.sourceOffset = copy.sourceOffset;
            command.destinationOffset = copy.destinationOffset;
            command.byteSize = copy.byteSize;
            packet.commands.push_back(command);
        }

        frame.passes.push_back(std::move(packet));
    }

    for (const auto& pass : frame.passes) {
        const bool hasCopy = std::any_of(
            pass.commands.begin(), pass.commands.end(),
            [](const FrameCommandPacket& command) {
                return command.type == FrameCommandType::CopyBuffer;
            });
        if (hasCopy && !std::all_of(
                pass.commands.begin(), pass.commands.end(),
                [](const FrameCommandPacket& command) {
                    return command.type == FrameCommandType::CopyBuffer;
                })) {
            frame.diagnostics.push_back(
                "Pass '" + pass.name +
                "' cannot mix copy commands with draw or dispatch commands");
        }
        for (const auto& command : pass.commands) {
            switch (command.type) {
            case FrameCommandType::Draw:
                if (command.vertexCount == 0 || command.instanceCount == 0) {
                    frame.diagnostics.push_back(
                        "Pass '" + pass.name + "' contains an empty draw");
                }
                break;
            case FrameCommandType::DrawIndexed:
                if (command.drawIndex >= pass.draws.size() ||
                    command.indexCount == 0 || command.instanceCount == 0) {
                    frame.diagnostics.push_back(
                        "Pass '" + pass.name +
                        "' contains an invalid indexed draw");
                }
                break;
            case FrameCommandType::DrawSkybox:
                break;
            case FrameCommandType::Dispatch:
                if (command.groupCountX == 0 || command.groupCountY == 0 ||
                    command.groupCountZ == 0) {
                    frame.diagnostics.push_back(
                        "Pass '" + pass.name +
                        "' contains an empty dispatch");
                }
                break;
            case FrameCommandType::CopyBuffer:
                if (command.sourceResourceId == 0 ||
                    command.destinationResourceId == 0 ||
                    command.sourceResourceId == command.destinationResourceId ||
                    command.byteSize == 0 || command.sourceOffset != 0 ||
                    command.destinationOffset != 0) {
                    frame.diagnostics.push_back(
                        "Pass '" + pass.name +
                        "' contains an invalid buffer copy");
                } else {
                    const auto declared = [&](RenderResourceId id)
                        -> const FrameBufferPacket* {
                        const auto found = std::find_if(
                            frame.buffers.begin(), frame.buffers.end(),
                            [&](const FrameBufferPacket& buffer) {
                                return buffer.id == id;
                            });
                        return found == frame.buffers.end() ? nullptr : &*found;
                    };
                    const auto* source = declared(command.sourceResourceId);
                    const auto* destination =
                        declared(command.destinationResourceId);
                    if (!source || !destination) {
                        frame.diagnostics.push_back(
                            "Pass '" + pass.name +
                            "' copies an undeclared buffer");
                    } else if (
                        (source->description.usageFlags &
                            PipelineBufferUsageTransferSource) == 0 ||
                        command.byteSize > source->description.byteSize ||
                        command.byteSize > destination->description.byteSize) {
                        frame.diagnostics.push_back(
                            "Pass '" + pass.name +
                            "' contains an out-of-range or non-transferable buffer copy");
                    }
                }
                break;
            }
        }
        for (const auto& write : pass.descriptorWrites) {
            const auto layout = std::find_if(
                pass.descriptorLayout.bindings.begin(),
                pass.descriptorLayout.bindings.end(),
                [&](const FrameDescriptorBinding& binding) {
                    return binding.binding == write.binding &&
                        binding.type == write.type;
                });
            if (layout == pass.descriptorLayout.bindings.end()) {
                frame.diagnostics.push_back(
                    "Pass '" + pass.name +
                    "' contains a descriptor write not present in its layout");
            }
        }
    }

    if (!frame.valid()) {
        frame.passes.clear();
        frame.textures.clear();
        frame.buffers.clear();
        frame.meshes.clear();
        frame.materials.clear();
        return frame;
    }

    return frame;
}

void FrameCompiler::resetHistory() {
    // Temporal transforms are owned by ViewState/GPUScene. FrameCompiler only
    // retains stable render-ID to GPU index allocation.
}

} // namespace Tasrovy::Render
