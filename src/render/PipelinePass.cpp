#include "PipelinePass.h"
#include "Object.h"
#include "Shader.h"
#include <algorithm>
#include <utility>

namespace Tasrovy::Render {

std::shared_ptr<PipelinePass> PipelinePass::create(const std::string& name) {
    return std::shared_ptr<PipelinePass>(new PipelinePass(name));
}

PipelinePass::PipelinePass(const std::string& name)
    : name_(name) {
}

void PipelinePass::setName(const std::string& name) { name_ = name; }
const std::string& PipelinePass::getName() const { return name_; }
void PipelinePass::setType(PipelinePassType type) { type_ = type; }
PipelinePassType PipelinePass::getType() const { return type_; }
void PipelinePass::setExecution(PipelinePassExecution execution) { execution_ = execution; }
PipelinePassExecution PipelinePass::getExecution() const { return execution_; }
void PipelinePass::setParameterProvider(std::string providerId) {
    parameterProvider_ = providerId.empty()
        ? ParameterProviders::Standard
        : std::move(providerId);
}
const std::string& PipelinePass::getParameterProvider() const {
    return parameterProvider_;
}
void PipelinePass::setViewIndex(uint32_t viewIndex) { viewIndex_ = viewIndex; }
uint32_t PipelinePass::getViewIndex() const { return viewIndex_; }
void PipelinePass::setVirtualShadowPage(const VirtualShadowPage& page) {
    virtualShadowPage_ = std::make_unique<VirtualShadowPage>(page);
}
const VirtualShadowPage* PipelinePass::getVirtualShadowPage() const {
    return virtualShadowPage_.get();
}

// Bulk
void PipelinePass::setState(const PassState& state) { state_ = state; }
const PassState& PipelinePass::getState() const { return state_; }

// --- Shaders ---
void PipelinePass::setVertexShader(std::shared_ptr<Shader> shader) {
    vertexShader_ = std::move(shader);
    state_.vertexPass.vertexShader = vertexShader_;
}
std::shared_ptr<Shader> PipelinePass::getVertexShader() const {
    return vertexShader_;
}
void PipelinePass::setFragmentShader(std::shared_ptr<Shader> shader) {
    fragmentShader_ = std::move(shader);
    state_.fragmentPass.fragmentShader = fragmentShader_;
}
std::shared_ptr<Shader> PipelinePass::getFragmentShader() const {
    return fragmentShader_;
}
void PipelinePass::setComputeShader(std::shared_ptr<Shader> shader) {
    computeShader_ = std::move(shader);
}
std::shared_ptr<Shader> PipelinePass::getComputeShader() const {
    return computeShader_;
}

void PipelinePass::addShaderPermutation(
    PipelineShaderPermutation permutation) {
    shaderPermutations_.push_back(std::move(permutation));
}

const std::vector<PipelineShaderPermutation>&
PipelinePass::getShaderPermutations() const {
    return shaderPermutations_;
}

void PipelinePass::setSelectedPermutationKey(uint64_t key) {
    selectedPermutationKey_ = key;
}

uint64_t PipelinePass::getSelectedPermutationKey() const {
    return selectedPermutationKey_;
}

void PipelinePass::setVertexLayout(PipelineVertexLayout layout) {
    vertexLayout_ = std::move(layout);
}

const PipelineVertexLayout& PipelinePass::getVertexLayout() const {
    return vertexLayout_;
}

void PipelinePass::setUniformByteSize(
    uint32_t byteSize,
    uint32_t shaderStages) {
    uniformByteSize_ = byteSize;
    uniformShaderStages_ = shaderStages;
}

uint32_t PipelinePass::getUniformByteSize() const {
    return uniformByteSize_;
}

uint32_t PipelinePass::getUniformShaderStages() const {
    return uniformShaderStages_;
}

void PipelinePass::addImportedTexture(
    PipelineImportedTextureBinding binding) {
    importedTextures_.push_back(std::move(binding));
}

const std::vector<PipelineImportedTextureBinding>&
PipelinePass::getImportedTextures() const {
    return importedTextures_;
}

void PipelinePass::setDispatch(
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ) {
    dispatch_ = std::make_unique<PipelineDispatchCommand>(
        PipelineDispatchCommand{
            std::max(1u, groupCountX),
            std::max(1u, groupCountY),
            std::max(1u, groupCountZ)
        });
}

const PipelineDispatchCommand* PipelinePass::getDispatch() const {
    return dispatch_.get();
}

void PipelinePass::addCopyCommand(PipelineCopyCommand command) {
    if (!command.source.empty() && !command.destination.empty() &&
        command.byteSize != 0) {
        copyCommands_.push_back(std::move(command));
    }
}

const std::vector<PipelineCopyCommand>& PipelinePass::getCopyCommands() const {
    return copyCommands_;
}

// --- Scene objects ---
void PipelinePass::addObject(std::weak_ptr<Object> object) {
    if (!object.expired()) {
        objects_.push_back(std::move(object));
    }
}

void PipelinePass::removeObject(const Object* object) {
    objects_.erase(
        std::remove_if(objects_.begin(), objects_.end(),
            [object](const std::weak_ptr<Object>& candidate) {
                const auto shared = candidate.lock();
                return !shared || shared.get() == object;
            }),
        objects_.end());
}

void PipelinePass::clearObjects() {
    objects_.clear();
}

const std::vector<std::weak_ptr<Object>>& PipelinePass::getObjects() const {
    return objects_;
}

// --- Logical texture resources ---
void PipelinePass::addSampledTexture(
    std::string slot,
    std::string resource,
    uint32_t binding,
    bool previousFrame,
    std::string producerPass) {
    if (binding == 0) {
        binding = static_cast<uint32_t>(sampledTextures_.size() + 1);
    }
    sampledTextures_.push_back({
        std::move(slot),
        std::move(resource),
        binding,
        previousFrame,
        std::move(producerPass)
    });
}

void PipelinePass::addStorageTexture(
    std::string slot,
    std::string resource,
    uint32_t binding,
    PipelineResourceAccess access,
    bool previousFrame) {
    if (access != PipelineResourceAccess::StorageRead &&
        access != PipelineResourceAccess::StorageWrite) {
        return;
    }
    storageTextures_.push_back({
        std::move(slot),
        std::move(resource),
        binding,
        access,
        previousFrame
    });
}

void PipelinePass::addStorageBuffer(
    std::string slot,
    std::string resource,
    uint32_t binding,
    PipelineResourceAccess access) {
    if (access != PipelineResourceAccess::BufferStorageRead &&
        access != PipelineResourceAccess::BufferStorageWrite) {
        return;
    }
    storageBuffers_.push_back({
        std::move(slot), std::move(resource), binding, access
    });
}

void PipelinePass::addColorAttachment(
    std::string resource,
    AttachmentLoad load,
    AttachmentStore store,
    std::string producerPass) {
    colorAttachments_.push_back({
        std::move(resource),
        load,
        store,
        std::move(producerPass)
    });
}

void PipelinePass::setDepthAttachment(
    std::string resource,
    AttachmentLoad load,
    AttachmentStore store,
    bool readOnly,
    float clearDepth,
    std::string producerPass) {
    depthAttachment_ = std::make_unique<DepthAttachmentRef>(
        DepthAttachmentRef{
            std::move(resource),
            load,
            store,
            readOnly,
            clearDepth,
            std::move(producerPass)
        });
}

void PipelinePass::addExecutionDependency(std::string producerPass) {
    if (!producerPass.empty() &&
        std::find(
            executionDependencies_.begin(),
            executionDependencies_.end(),
            producerPass) == executionDependencies_.end()) {
        executionDependencies_.push_back(std::move(producerPass));
    }
}

const std::vector<std::string>&
PipelinePass::getExecutionDependencies() const {
    return executionDependencies_;
}

const std::vector<SampledTextureInput>& PipelinePass::getSampledTextures() const {
    return sampledTextures_;
}

const std::vector<ColorAttachmentRef>& PipelinePass::getColorAttachments() const {
    return colorAttachments_;
}

const DepthAttachmentRef* PipelinePass::getDepthAttachment() const {
    return depthAttachment_.get();
}

std::vector<PipelineResourceRef> PipelinePass::getReadResources() const {
    std::vector<PipelineResourceRef> resources;
    resources.reserve(
        sampledTextures_.size() + storageTextures_.size() +
        storageBuffers_.size() +
        colorAttachments_.size() + copyCommands_.size() +
        (depthAttachment_ ? 1 : 0));

    for (const auto& input : sampledTextures_) {
        resources.push_back({
            input.slot,
            input.resource,
            input.binding,
            PipelineResourceAccess::SampledRead,
            input.previousFrame,
            input.producerPass
        });
    }
    for (const auto& storage : storageTextures_) {
        if (storage.access == PipelineResourceAccess::StorageRead) {
            resources.push_back(storage);
        }
    }
    for (const auto& storage : storageBuffers_) {
        if (storage.access == PipelineResourceAccess::BufferStorageRead) {
            resources.push_back(storage);
        }
    }
    for (const auto& attachment : colorAttachments_) {
        if (attachment.load == AttachmentLoad::Load) {
            resources.push_back({
                "",
                attachment.resource,
                0,
                PipelineResourceAccess::ColorRead,
                false,
                attachment.producerPass
            });
        }
    }
    if (depthAttachment_ &&
        (depthAttachment_->readOnly || depthAttachment_->load == AttachmentLoad::Load)) {
        resources.push_back({
            "",
            depthAttachment_->resource,
            0,
            PipelineResourceAccess::DepthRead,
            false,
            depthAttachment_->producerPass
        });
    }

    for (const auto& copy : copyCommands_) {
        resources.push_back({
            "source", copy.source, 0,
            PipelineResourceAccess::BufferTransferRead
        });
    }

    return resources;
}

std::vector<PipelineResourceRef> PipelinePass::getWriteResources() const {
    std::vector<PipelineResourceRef> resources;
    resources.reserve(
        storageTextures_.size() + storageBuffers_.size() +
        colorAttachments_.size() +
        copyCommands_.size() + (depthAttachment_ ? 1 : 0));

    for (const auto& storage : storageTextures_) {
        if (storage.access == PipelineResourceAccess::StorageWrite) {
            resources.push_back(storage);
        }
    }
    for (const auto& storage : storageBuffers_) {
        if (storage.access == PipelineResourceAccess::BufferStorageWrite) {
            resources.push_back(storage);
        }
    }
    for (const auto& attachment : colorAttachments_) {
        resources.push_back({
            "",
            attachment.resource,
            0,
            PipelineResourceAccess::ColorWrite
        });
    }
    if (depthAttachment_ && !depthAttachment_->readOnly) {
        resources.push_back({"", depthAttachment_->resource, 0, PipelineResourceAccess::DepthWrite});
    }
    for (const auto& copy : copyCommands_) {
        resources.push_back({
            "destination", copy.destination, 0,
            PipelineResourceAccess::BufferTransferWrite
        });
    }

    return resources;
}

// --- Per-material texture inputs ---
void PipelinePass::addMaterialTexture(MaterialTextureRequirement requirement) {
    materialTextures_.push_back(std::move(requirement));
}

const std::vector<MaterialTextureRequirement>& PipelinePass::getMaterialTextures() const {
    return materialTextures_;
}

// --- Vertex input ---
void PipelinePass::setTopology(Topology t) { state_.vertexInput.topology = t; }
Topology PipelinePass::getTopology() const { return state_.vertexInput.topology; }

// --- Rasterization ---
void PipelinePass::setCullMode(CullMode c) { state_.rasterizationPass.cullMode = c; }
CullMode PipelinePass::getCullMode() const { return state_.rasterizationPass.cullMode; }

// --- Depth ---
void PipelinePass::setDepthTest(bool enable) { state_.depthPass.depthTest = enable; }
void PipelinePass::setDepthWrite(bool enable) { state_.depthPass.depthWrite = enable; }
void PipelinePass::setDepthTestMode(DepthTestMode mode) { state_.depthPass.testMode = mode; }
bool PipelinePass::getDepthTest() const { return state_.depthPass.depthTest; }
bool PipelinePass::getDepthWrite() const { return state_.depthPass.depthWrite; }
DepthTestMode PipelinePass::getDepthTestMode() const { return state_.depthPass.testMode; }

// --- Color blending ---
void PipelinePass::setBlendMode(BlendMode b) { state_.colorBlendingPass.blendMode = b; }
void PipelinePass::setClearColor(const TSVec4f& color) { state_.colorBlendingPass.clearColor = color; }
BlendMode PipelinePass::getBlendMode() const { return state_.colorBlendingPass.blendMode; }
const TSVec4f& PipelinePass::getClearColor() const { return state_.colorBlendingPass.clearColor; }

} // namespace Tasrovy::Render
