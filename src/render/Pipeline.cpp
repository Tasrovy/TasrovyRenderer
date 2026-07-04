#include "Pipeline.h"
#include "PipelinePass.h"
#include <algorithm>

namespace Tasrovy {

std::shared_ptr<Pipeline> Pipeline::create(const std::string& name) {
    return std::shared_ptr<Pipeline>(new Pipeline(name));
}

Pipeline::Pipeline(const std::string& name)
    : name_(name) {
}

void Pipeline::setName(const std::string& name) { name_ = name; }
const std::string& Pipeline::getName() const { return name_; }

void Pipeline::addPass(std::shared_ptr<PipelinePass> pass) {
    passes_.push_back(std::move(pass));
}

void Pipeline::insertPass(size_t index, std::shared_ptr<PipelinePass> pass) {
    if (index >= passes_.size()) {
        passes_.push_back(std::move(pass));
    } else {
        passes_.insert(passes_.begin() + index, std::move(pass));
    }
}

void Pipeline::removePass(const std::string& passName) {
    passes_.erase(
        std::remove_if(passes_.begin(), passes_.end(),
            [&passName](const std::shared_ptr<PipelinePass>& p) {
                return p->getName() == passName;
            }),
        passes_.end());
}

void Pipeline::removePass(size_t index) {
    if (index < passes_.size()) {
        passes_.erase(passes_.begin() + index);
    }
}

void Pipeline::clearPasses() { passes_.clear(); }

void Pipeline::movePass(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= passes_.size() || toIndex >= passes_.size() || fromIndex == toIndex) {
        return;
    }
    auto pass = std::move(passes_[fromIndex]);
    passes_.erase(passes_.begin() + fromIndex);
    passes_.insert(passes_.begin() + toIndex, std::move(pass));
}

std::shared_ptr<PipelinePass> Pipeline::getPass(const std::string& name) const {
    for (const auto& p : passes_) {
        if (p->getName() == name) {
            return p;
        }
    }
    return nullptr;
}

std::shared_ptr<PipelinePass> Pipeline::getPass(size_t index) const {
    return index < passes_.size() ? passes_[index] : nullptr;
}

size_t Pipeline::getPassCount() const { return passes_.size(); }

const std::vector<std::shared_ptr<PipelinePass>>& Pipeline::getPasses() const { return passes_; }

bool Pipeline::hasPass(const std::string& name) const {
    return getPass(name) != nullptr;
}

} // namespace Tasrovy
