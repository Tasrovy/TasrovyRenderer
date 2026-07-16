#pragma once

#include <string>
#include <vector>
#include <memory>

#include "PipelineResource.h"
#include "Scene.h"

namespace Tasrovy::Render {

class PipelinePass;

struct PipelinePassDependency {
    std::string producerPass;
    std::string consumerPass;
    std::string resource;
};

class PipelineBase : public std::enable_shared_from_this<PipelineBase> {
public:
    virtual ~PipelineBase() = default;

    void setName(const std::string& name);
    const std::string& getName() const;

    virtual void GenPass(std::shared_ptr<Scene> scene) = 0;

    std::shared_ptr<PipelinePass> getPass(const std::string& name) const;
    std::shared_ptr<PipelinePass> getPass(size_t index) const;
    size_t getPassCount() const;
    const std::vector<std::shared_ptr<PipelinePass>>& getPasses() const;
    bool hasPass(const std::string& name) const;

    const PipelineTextureDesc* getTexture(const std::string& name) const;
    const std::vector<PipelineTextureDesc>& getTextures() const;
    std::vector<PipelinePassDependency> getPassDependencies() const;
    std::vector<std::string> validatePassDependencies() const;
    std::vector<std::string> validateResourceFlow() const;

protected:
    PipelineBase() = default;
    explicit PipelineBase(const std::string& name);

    void addPass(std::shared_ptr<PipelinePass> pass);
    void clearPasses();
    void declareTexture(PipelineTextureDesc desc);
    void clearTextures();

    std::string name_;
    std::vector<std::shared_ptr<PipelinePass>> passes_;
    std::vector<PipelineTextureDesc> textures_;
};

} // namespace Tasrovy::Render
