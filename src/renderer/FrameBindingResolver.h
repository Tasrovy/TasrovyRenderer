#pragma once

#include "../RHI/FrameExecutor.h"

#include <vector>
#include <functional>
#include <string>

namespace Tasrovy::RHI {
class CompiledPassResources;
class Device;
}

namespace Tasrovy::Render {
struct FrameSourceRegistry;
}

namespace Tasrovy::Renderer {

class SceneGPUResources;
struct RendererSettings;
struct ViewState;

// Resolves stable render IDs to scene-lifetime GPU resources and produces the
// API-independent external bindings consumed by the selected RHI executor.
class FrameBindingResolver {
public:
    using ImportedResourceProvider = std::function<
        Tasrovy::RHI::FrameImportedImageBinding(
            Tasrovy::RHI::Device&,
            SceneGPUResources&)>;

    static void registerImportedResource(
        std::string handle,
        ImportedResourceProvider provider);
    static bool hasImportedResource(const std::string& handle);
    static Tasrovy::RHI::FrameExecutionBindings resolve(
        Tasrovy::RHI::Device& device,
        SceneGPUResources& sceneResources,
        const Tasrovy::Render::FrameSourceRegistry& sources,
        const std::vector<Tasrovy::RHI::CompiledPassResources*>& passes,
        const RendererSettings& settings,
        const ViewState& viewState);
};

} // namespace Tasrovy::Renderer
