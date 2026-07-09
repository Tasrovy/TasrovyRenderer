#include "PipelinePass.h"
#include "Object.h"
#include "Shader.h"
#include <algorithm>

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
void PipelinePass::addSampledTexture(std::string slot, std::string resource) {
    sampledTextures_.push_back({std::move(slot), std::move(resource)});
}

void PipelinePass::addColorAttachment(
    std::string resource,
    AttachmentLoad load,
    AttachmentStore store) {
    colorAttachments_.push_back({std::move(resource), load, store});
}

void PipelinePass::setDepthAttachment(
    std::string resource,
    AttachmentLoad load,
    AttachmentStore store,
    bool readOnly,
    float clearDepth) {
    depthAttachment_ = std::make_unique<DepthAttachmentRef>(
        DepthAttachmentRef{std::move(resource), load, store, readOnly, clearDepth});
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
