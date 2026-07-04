#include "PipelinePass.h"
#include "Shader.h"
#include "Object.h"
#include <algorithm>

namespace Tasrovy {

std::shared_ptr<PipelinePass> PipelinePass::create(const std::string& name) {
    return std::shared_ptr<PipelinePass>(new PipelinePass(name));
}

PipelinePass::PipelinePass(const std::string& name)
    : name_(name) {
}

void PipelinePass::setName(const std::string& name) { name_ = name; }
const std::string& PipelinePass::getName() const { return name_; }

void PipelinePass::setShader(std::weak_ptr<Shader> shader) { shader_ = shader; }
std::shared_ptr<Shader> PipelinePass::getShader() const { return shader_.lock(); }

void PipelinePass::setInputTexture(const std::string& name, const std::string& resourcePath) {
    inputTextures_.push_back({ name, resourcePath });
}

void PipelinePass::setOutputTexture(const std::string& name, const std::string& resourcePath) {
    outputTextures_.push_back({ name, resourcePath });
}

const std::vector<TextureSlot>& PipelinePass::getInputTextures() const { return inputTextures_; }
const std::vector<TextureSlot>& PipelinePass::getOutputTextures() const { return outputTextures_; }

void PipelinePass::addObject(std::weak_ptr<Object> object) {
    objects_.push_back(std::move(object));
}

void PipelinePass::removeObject(Object* object) {
    objects_.erase(
        std::remove_if(objects_.begin(), objects_.end(),
            [object](const std::weak_ptr<Object>& wp) {
                auto sp = wp.lock();
                return !sp || sp.get() == object;
            }),
        objects_.end());
}

const std::vector<std::weak_ptr<Object>>& PipelinePass::getObjects() const { return objects_; }

void PipelinePass::setState(const PassState& state) { state_ = state; }
const PassState& PipelinePass::getState() const { return state_; }

} // namespace Tasrovy
