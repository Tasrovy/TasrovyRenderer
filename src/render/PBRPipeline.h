#pragma once
#include "Pipeline.h"

namespace Tasrovy::Render {
    class PBRPipeline : public PipelineBase{
    public:
        static std::shared_ptr<PBRPipeline> create(const std::string& name = "PBR");
        void GenPass(std::shared_ptr<Scene> scene) override;
    protected:
        PBRPipeline() = default;
        PBRPipeline(const std::string& name);
    };
}
