#include "Pipeline.h"
#include "PipelinePass.h"
#include <unordered_map>
#include <unordered_set>

namespace Tasrovy::Render {
PipelineBase::PipelineBase(const std::string& name)
    : name_(name) {
}

void PipelineBase::setName(const std::string& name) { name_ = name; }
const std::string& PipelineBase::getName() const { return name_; }

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

std::vector<PipelinePassDependency> PipelineBase::getPassDependencies() const {
    std::vector<PipelinePassDependency> dependencies;
    std::unordered_map<std::string, std::string> latestWriter;

    for (const auto& texture : textures_) {
        if (texture.external && !texture.name.empty()) {
            latestWriter[texture.name] = "External";
        }
    }

    for (const auto& pass : passes_) {
        if (!pass) {
            continue;
        }

        const auto& passName = pass->getName();
        for (const auto& resource : pass->getReadResources()) {
            const auto writer = latestWriter.find(resource.resource);
            if (writer != latestWriter.end()) {
                dependencies.push_back({
                    writer->second,
                    passName,
                    resource.resource
                });
            }
        }

        for (const auto& resource : pass->getWriteResources()) {
            latestWriter[resource.resource] = passName;
        }
    }

    return dependencies;
}

std::vector<std::string> PipelineBase::validatePassDependencies() const {
    std::vector<std::string> errors;
    std::unordered_set<std::string> available;
    std::unordered_set<std::string> textureNames;
    std::unordered_set<std::string> passNames;
    std::unordered_map<std::string, std::vector<size_t>> writersByResource;

    for (size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
        const auto& pass = passes_[passIndex];
        if (!pass) {
            errors.push_back("Pipeline has a null pass at index " + std::to_string(passIndex));
            continue;
        }
        for (const auto& resource : pass->getWriteResources()) {
            writersByResource[resource.resource].push_back(passIndex);
        }
    }

    for (const auto& texture : textures_) {
        if (texture.name.empty()) {
            errors.emplace_back("Pipeline texture has an empty name");
            continue;
        }
        if (!textureNames.insert(texture.name).second) {
            errors.push_back("Pipeline texture '" + texture.name + "' is declared more than once");
        } else if (texture.external) {
            available.insert(texture.name);
        }
    }

    const auto requireDeclared = [&](const std::string& resource, const std::string& passName) {
        if (!getTexture(resource)) {
            errors.push_back(
                "Pass '" + passName + "' references undeclared texture '" + resource + "'");
            return false;
        }
        return true;
    };

    const auto requireAvailable = [&](
        const std::string& resource,
        const std::string& passName,
        size_t passIndex) {
        if (!available.contains(resource)) {
            const auto futureWriter = writersByResource.find(resource);
            if (futureWriter != writersByResource.end()) {
                for (const auto writerIndex : futureWriter->second) {
                    if (writerIndex > passIndex && writerIndex < passes_.size() && passes_[writerIndex]) {
                        errors.push_back(
                            "Pass '" + passName + "' reads texture '" + resource +
                            "' before producer pass '" + passes_[writerIndex]->getName() + "'");
                        return false;
                    }
                }
            }
            errors.push_back(
                "Pass '" + passName + "' reads texture '" + resource +
                "' before any producer pass");
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
        std::unordered_set<uint32_t> sampledBindings;
        std::unordered_set<std::string> writes;

        if (passName.empty()) {
            errors.push_back("Pipeline pass at index " + std::to_string(passIndex) + " has an empty name");
        } else if (!passNames.insert(passName).second) {
            errors.push_back("Pipeline pass '" + passName + "' is declared more than once");
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

        for (const auto& resource : pass->getReadResources()) {
            if (requireDeclared(resource.resource, passName)) {
                requireAvailable(resource.resource, passName, passIndex);
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
        }

        for (const auto& resource : pass->getWriteResources()) {
            if (!writes.insert(resource.resource).second) {
                errors.push_back(
                    "Pass '" + passName + "' writes texture '" + resource.resource +
                    "' more than once");
            }
            if (requireDeclared(resource.resource, passName)) {
                available.insert(resource.resource);
            }
        }
    }

    return errors;
}

std::vector<std::string> PipelineBase::validateResourceFlow() const {
    return validatePassDependencies();
}

} // namespace Tasrovy::Render
