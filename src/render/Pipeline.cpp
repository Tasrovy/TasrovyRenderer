#include "Pipeline.h"
#include "PipelinePass.h"
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

std::vector<std::string> PipelineBase::validateResourceFlow() const {
    std::vector<std::string> errors;
    std::unordered_set<std::string> available;

    for (const auto& texture : textures_) {
        if (texture.name.empty()) {
            errors.emplace_back("Pipeline texture has an empty name");
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

    const auto requireAvailable = [&](const std::string& resource, const std::string& passName) {
        if (!available.contains(resource)) {
            errors.push_back(
                "Pass '" + passName + "' reads texture '" + resource +
                "' before it is produced");
            return false;
        }
        return true;
    };

    for (const auto& pass : passes_) {
        const auto& passName = pass->getName();
        std::unordered_set<std::string> materialSlots;

        for (const auto& input : pass->getSampledTextures()) {
            if (requireDeclared(input.resource, passName)) {
                requireAvailable(input.resource, passName);
            }
        }

        for (const auto& attachment : pass->getColorAttachments()) {
            if (!requireDeclared(attachment.resource, passName)) {
                continue;
            }
            if (attachment.load == AttachmentLoad::Load) {
                requireAvailable(attachment.resource, passName);
            }
            if (attachment.store == AttachmentStore::Store) {
                available.insert(attachment.resource);
            } else {
                available.erase(attachment.resource);
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

        if (const auto* depth = pass->getDepthAttachment()) {
            if (!requireDeclared(depth->resource, passName)) {
                continue;
            }
            if (depth->load == AttachmentLoad::Load || depth->readOnly) {
                requireAvailable(depth->resource, passName);
            }
            if (!depth->readOnly) {
                if (depth->store == AttachmentStore::Store) {
                    available.insert(depth->resource);
                } else {
                    available.erase(depth->resource);
                }
            }
        }
    }

    return errors;
}

} // namespace Tasrovy::Render
