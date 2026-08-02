#pragma once

#include "Pipeline.h"

#include <cstdint>

namespace Tasrovy::Render {

enum class DeferredShadowTechnique : uint8_t {
    ShadowMap,
    CascadedShadowMap,
    VirtualShadowMap
};

// Render-side feature selection. SceneRenderer supplies this policy before
// GenPass(), so disabled passes never enter RenderGraph or the RHI plan.
struct DeferredPipelineConfig {
    DeferredShadowTechnique shadowTechnique =
        DeferredShadowTechnique::VirtualShadowMap;
    bool hbao = true;
    bool hiZ = false;
    bool ssr = false;
    bool depthOfField = false;
    uint8_t temporalMode = 2;
    bool motionBlur = false;
    bool outline = true;
    bool bloom = true;

    bool operator==(const DeferredPipelineConfig&) const = default;
};

class DeferredPipeline : public PipelineBase {
public:
    static std::shared_ptr<DeferredPipeline> create(const std::string& name = "Deferred");
    void setConfig(const DeferredPipelineConfig& config);
    const DeferredPipelineConfig& getConfig() const;
    bool applyConfiguration(
        const PipelineConfiguration& configuration) override;
    void GenPass(std::shared_ptr<Scene> scene) override;

protected:
    DeferredPipeline() = default;
    explicit DeferredPipeline(const std::string& name);

private:
    DeferredPipelineConfig config_;
};

} // namespace Tasrovy::Render
