#pragma once

#include "RendererSettings.h"
#include "../render/Pipeline.h"

namespace Tasrovy::Renderer {

// Translates user-facing settings into the immutable feature set used while
// producing a RenderGraph. Execution code never performs feature culling.
class RendererFeaturePolicy {
public:
    static Render::PipelineConfiguration configuration(
        const RendererSettings& settings);
};

} // namespace Tasrovy::Renderer
