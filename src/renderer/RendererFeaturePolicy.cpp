#include "RendererFeaturePolicy.h"

#include <algorithm>

namespace Tasrovy::Renderer {

Render::PipelineConfiguration RendererFeaturePolicy::configuration(
    const RendererSettings& settings) {
    using namespace Render;
    PipelineConfiguration config;
    config.values.emplace(
        PipelineConfigKeys::ShadowTechnique,
        static_cast<int64_t>(std::clamp(settings.shadowTechnique, 0, 2)));
    config.values.emplace(PipelineConfigKeys::Hbao, settings.ssaoEnabled);
    config.values.emplace(PipelineConfigKeys::Ssr, settings.ssrEnabled);
    config.values.emplace(
        PipelineConfigKeys::HiZ,
        settings.ssrEnabled ||
        settings.debugOutputSemantic == DebugTextureSemantic::HiZLinearDepth);
    config.values.emplace(
        PipelineConfigKeys::DepthOfField,
        settings.depthOfFieldEnabled);
    config.values.emplace(
        PipelineConfigKeys::TemporalMode,
        static_cast<int64_t>(
            std::clamp(settings.temporalAAMode, 0, 2)));
    config.values.emplace(
        PipelineConfigKeys::MotionBlur,
        settings.motionBlurEnabled);
    config.values.emplace(
        PipelineConfigKeys::Outline,
        settings.outlineEnabled ||
            settings.debugOutputSemantic ==
                DebugTextureSemantic::OutlineBlackLines);
    config.values.emplace(PipelineConfigKeys::Bloom, settings.bloomEnabled);
    return config;
}

} // namespace Tasrovy::Renderer
