#include "Pipeline.h"
#include "PipelinePass.h"
#include "RenderGraph.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Tasrovy::Render {
PipelineBase::PipelineBase(const std::string& name)
    : name_(name) {
}

void PipelineBase::setName(const std::string& name) { name_ = name; }
const std::string& PipelineBase::getName() const { return name_; }

bool PipelineBase::applyConfiguration(
    const PipelineConfiguration& configuration) {
    if (configuration_ == configuration) return false;
    commitConfiguration(configuration);
    return true;
}

uint64_t PipelineBase::getConfigurationVersion() const {
    return configurationVersion_;
}

void PipelineBase::commitConfiguration(
    const PipelineConfiguration& configuration) {
    configuration_ = configuration;
    markConfigurationDirty();
}

void PipelineBase::markConfigurationDirty() {
    ++configurationVersion_;
    if (configurationVersion_ == 0) configurationVersion_ = 1;
}

void PipelineBase::addPass(std::shared_ptr<PipelinePass> pass) {
    passes_.push_back(std::move(pass));
}

void PipelineBase::clearPasses() {
    passes_.clear();
}

std::shared_ptr<PipelinePass> PipelineBase::getPass(const std::string& name) const {
    for (const auto& pass : passes_) {
        if (pass->getName() == name) {
            return pass;
        }
    }
    return nullptr;
}

std::shared_ptr<PipelinePass> PipelineBase::getPass(size_t index) const {
    return index < passes_.size() ? passes_[index] : nullptr;
}

size_t PipelineBase::getPassCount() const {
    return passes_.size();
}

const std::vector<std::shared_ptr<PipelinePass>>& PipelineBase::getPasses() const {
    return passes_;
}

bool PipelineBase::hasPass(const std::string& name) const {
    return getPass(name) != nullptr;
}

void PipelineBase::declareTexture(PipelineTextureDesc desc) {
    for (auto& texture : textures_) {
        if (texture.name == desc.name) {
            texture = std::move(desc);
            return;
        }
    }
    textures_.push_back(std::move(desc));
}

void PipelineBase::clearTextures() {
    textures_.clear();
}

const PipelineTextureDesc* PipelineBase::getTexture(const std::string& name) const {
    for (const auto& texture : textures_) {
        if (texture.name == name) {
            return &texture;
        }
    }
    return nullptr;
}

const std::vector<PipelineTextureDesc>& PipelineBase::getTextures() const {
    return textures_;
}

void PipelineBase::declareBuffer(PipelineBufferDesc desc) {
    for (auto& buffer : buffers_) {
        if (buffer.name == desc.name) {
            buffer = std::move(desc);
            return;
        }
    }
    buffers_.push_back(std::move(desc));
}

void PipelineBase::clearBuffers() { buffers_.clear(); }

const PipelineBufferDesc* PipelineBase::getBuffer(
    const std::string& name) const {
    const auto found = std::find_if(
        buffers_.begin(), buffers_.end(),
        [&](const PipelineBufferDesc& buffer) {
            return buffer.name == name;
        });
    return found == buffers_.end() ? nullptr : &*found;
}

const std::vector<PipelineBufferDesc>& PipelineBase::getBuffers() const {
    return buffers_;
}

std::vector<PipelinePassDependency> PipelineBase::getPassDependencies() const {
    std::vector<PipelinePassDependency> dependencies;
    const auto graph = RenderGraph::compile(*this);
    for (const auto& edge : graph.getEdges()) {
        if (edge.producer >= graph.getNodes().size() ||
            edge.consumer >= graph.getNodes().size()) {
            continue;
        }
        dependencies.push_back({
            graph.getNodes()[edge.producer].pass->getName(),
            graph.getNodes()[edge.consumer].pass->getName(),
            edge.resource
        });
    }
    return dependencies;
}

std::vector<std::string> PipelineBase::validatePassDependencies() const {
    std::vector<std::string> errors;
    std::unordered_set<std::string> textureNames;
    std::unordered_set<std::string> externalTextures;
    std::unordered_set<std::string> bufferNames;
    std::unordered_set<std::string> externalBuffers;
    std::unordered_set<std::string> passNames;
    std::unordered_map<std::string, size_t> passIndices;
    std::unordered_map<std::string, std::vector<size_t>> writersByResource;

    for (const auto& texture : textures_) {
        if (texture.name.empty()) {
            errors.emplace_back("Pipeline texture has an empty name");
            continue;
        }
        if (!textureNames.insert(texture.name).second) {
            errors.push_back("Pipeline texture '" + texture.name + "' is declared more than once");
        }
        if (texture.external) {
            externalTextures.insert(texture.name);
        }
    }

    for (const auto& buffer : buffers_) {
        if (buffer.name.empty() || buffer.byteSize == 0) {
            errors.emplace_back("Pipeline buffer has an empty name or size");
            continue;
        }
        if (!bufferNames.insert(buffer.name).second ||
            textureNames.contains(buffer.name)) {
            errors.push_back(
                "Pipeline resource '" + buffer.name +
                "' is declared more than once");
        }
        if (buffer.external || buffer.hostVisible) {
            externalBuffers.insert(buffer.name);
        }
    }

    for (size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
        const auto& pass = passes_[passIndex];
        if (!pass) {
            errors.push_back(
                "Pipeline has a null pass at index " +
                std::to_string(passIndex));
            continue;
        }
        const auto& passName = pass->getName();
        if (passName.empty()) {
            errors.push_back(
                "Pipeline pass at index " +
                std::to_string(passIndex) +
                " has an empty name");
        } else if (!passNames.insert(passName).second) {
            errors.push_back(
                "Pipeline pass '" + passName +
                "' is declared more than once");
        } else {
            passIndices.emplace(passName, passIndex);
        }
        for (const auto& resource : pass->getWriteResources()) {
            writersByResource[resource.resource].push_back(passIndex);
        }
    }

    const auto requireDeclared = [&](
        const PipelineResourceRef& resource,
        const std::string& passName) {
        const bool declared = isBufferAccess(resource.access)
            ? getBuffer(resource.resource) != nullptr
            : getTexture(resource.resource) != nullptr;
        if (!declared) {
            errors.push_back(
                "Pass '" + passName + "' references undeclared " +
                (isBufferAccess(resource.access) ? "buffer '" : "texture '") +
                resource.resource + "'");
            return false;
        }
        return true;
    };

    for (size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
        const auto& pass = passes_[passIndex];
        if (!pass) {
            continue;
        }

        const auto& passName = pass->getName();
        std::unordered_set<std::string> materialSlots;
        std::unordered_set<uint32_t> materialBindings;
        std::unordered_set<uint32_t> sampledBindings;
        std::unordered_set<std::string> writes;
        const auto readResources = pass->getReadResources();
        const auto writeResources = pass->getWriteResources();

        // A shader may not read a current-frame texture that the same pass
        // also modifies. Vulkan attachment Load followed by attachment writes
        // remains legal: it is a render-target load/store operation rather
        // than a sampled/storage feedback loop.
        for (const auto& read : readResources) {
            if (read.previousFrame ||
                (read.access != PipelineResourceAccess::SampledRead &&
                 read.access != PipelineResourceAccess::StorageRead &&
                 read.access !=
                    PipelineResourceAccess::BufferStorageRead)) {
                continue;
            }
            const bool alsoWritten = std::any_of(
                writeResources.begin(),
                writeResources.end(),
                [&](const PipelineResourceRef& write) {
                    return write.resource == read.resource;
                });
            if (alsoWritten) {
                errors.push_back(
                    "Pass '" + passName +
                    "' cannot read texture '" + read.resource +
                    "' as a shader-readable input while modifying it in the "
                    "same pass");
            }
        }

        for (const auto& dependency : pass->getExecutionDependencies()) {
            const auto producer = passIndices.find(dependency);
            if (producer == passIndices.end()) {
                errors.push_back(
                    "Pass '" + passName +
                    "' depends on unknown pass '" + dependency + "'");
            } else if (producer->second == passIndex) {
                errors.push_back(
                    "Pass '" + passName +
                    "' cannot depend on itself");
            }
        }

        for (const auto& input : pass->getSampledTextures()) {
            if (input.slot.empty()) {
                errors.push_back("Pass '" + passName + "' has a sampled texture with an empty slot");
            }
            if (!sampledBindings.insert(input.binding).second) {
                errors.push_back(
                    "Pass '" + passName + "' has duplicate sampled texture binding " +
                    std::to_string(input.binding));
            }
        }

        for (const auto& imported : pass->getImportedTextures()) {
            if (imported.handle.empty()) {
                errors.push_back(
                    "Pass '" + passName +
                    "' has an imported texture with an empty handle");
            }
            if (imported.binding == 0) {
                errors.push_back(
                    "Pass '" + passName +
                    "' has imported texture '" + imported.handle +
                    "' at reserved binding 0");
            } else if (!sampledBindings.insert(imported.binding).second) {
                errors.push_back(
                    "Pass '" + passName + "' reuses binding " +
                    std::to_string(imported.binding) +
                    " for multiple sampled/imported textures");
            }
            if (imported.shaderStages == 0) {
                errors.push_back(
                    "Pass '" + passName +
                    "' has imported texture '" + imported.handle +
                    "' with no shader stages");
            }
        }

        for (const auto& resource : readResources) {
            if (!requireDeclared(resource, passName) ||
                resource.previousFrame) {
                continue;
            }

            if (!resource.producerPass.empty()) {
                const auto producer = passIndices.find(resource.producerPass);
                if (producer == passIndices.end()) {
                    errors.push_back(
                        "Pass '" + passName +
                        "' reads texture '" + resource.resource +
                        "' from unknown producer pass '" +
                        resource.producerPass + "'");
                    continue;
                }
                const auto writers = writersByResource.find(resource.resource);
                const bool writesResource =
                    writers != writersByResource.end() &&
                    std::find(
                        writers->second.begin(),
                        writers->second.end(),
                        producer->second) != writers->second.end();
                if (!writesResource) {
                    errors.push_back(
                        "Pass '" + passName +
                        "' names pass '" + resource.producerPass +
                        "' as producer of texture '" + resource.resource +
                        "', but that pass does not write it");
                }
                continue;
            }

            size_t producerCount = 0;
            if (const auto writers =
                    writersByResource.find(resource.resource);
                writers != writersByResource.end()) {
                for (const size_t writer : writers->second) {
                    if (writer != passIndex) {
                        ++producerCount;
                    }
                }
            }
            const bool external = isBufferAccess(resource.access)
                ? externalBuffers.contains(resource.resource)
                : externalTextures.contains(resource.resource);
            if (producerCount == 0 && !external) {
                errors.push_back(
                    "Pass '" + passName +
                    "' reads texture '" + resource.resource +
                    "' without a current-frame producer");
            } else if (producerCount > 1) {
                errors.push_back(
                    "Pass '" + passName +
                    "' reads multi-writer texture '" +
                    resource.resource +
                    "' without naming a producer pass");
            }
        }

        for (const auto& materialTexture : pass->getMaterialTextures()) {
            if (materialTexture.slot.empty()) {
                errors.push_back(
                    "Pass '" + passName + "' has a material texture with an empty slot");
            } else if (!materialSlots.insert(materialTexture.slot).second) {
                errors.push_back(
                    "Pass '" + passName + "' has duplicate material texture slot '" +
                    materialTexture.slot + "'");
            }
            if (materialTexture.binding == 0) {
                errors.push_back(
                    "Pass '" + passName + "' has material texture slot '" +
                    materialTexture.slot + "' at reserved binding 0");
            } else if (!materialBindings.insert(materialTexture.binding).second) {
                errors.push_back(
                    "Pass '" + passName +
                    "' has duplicate material texture binding " +
                    std::to_string(materialTexture.binding));
            }
            if (sampledBindings.contains(materialTexture.binding)) {
                errors.push_back(
                    "Pass '" + passName +
                    "' reuses binding " +
                    std::to_string(materialTexture.binding) +
                    " for sampled and material textures");
            }
        }

        for (const auto& resource : writeResources) {
            if (!writes.insert(resource.resource).second) {
                errors.push_back(
                    "Pass '" + passName + "' writes texture '" + resource.resource +
                    "' more than once");
            }
            if (requireDeclared(resource, passName)) {
                // Declaration validation is intentionally independent of
                // addPass() order. RenderGraph resolves producer versions.
            }
        }
    }

    return errors;
}

std::vector<std::string> PipelineBase::validateResourceFlow() const {
    const auto graph = RenderGraph::compile(*this);
    return graph.getDiagnostics();
}

} // namespace Tasrovy::Render
