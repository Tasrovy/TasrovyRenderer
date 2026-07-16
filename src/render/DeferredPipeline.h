#pragma once

#include "Pipeline.h"

namespace Tasrovy::Render {

class DeferredPipeline : public PipelineBase {
public:
    static std::shared_ptr<DeferredPipeline> create(const std::string& name = "Deferred");
    void GenPass(std::shared_ptr<Scene> scene) override;

protected:
    DeferredPipeline() = default;
    explicit DeferredPipeline(const std::string& name);
};

} // namespace Tasrovy::Render
