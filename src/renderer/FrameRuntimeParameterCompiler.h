#pragma once

#include "../base/TSMatrix.h"
#include "FrameParameterBuilder.h"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <functional>
#include <string>

namespace Tasrovy::Render {
class Camera;
class Object;
class Scene;
class Material;
struct FramePassPacket;
}

namespace Tasrovy::Renderer {

struct SceneRendererComponents;
struct ViewFrameData;
struct ViewState;

struct FrameParameterProviderContext {
    Tasrovy::Render::FramePassPacket& pass;
    FrameUniformBuffer& uniform;
    std::vector<std::byte>& output;
    const RendererSettings& settings;
    const ViewState& viewState;
    const ViewFrameData& viewFrame;
    Tasrovy::Render::Camera& camera;
    uint32_t internalWidth = 0;
    uint32_t internalHeight = 0;
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;
    bool outputOverridden = false;
};

using FrameParameterProvider =
    std::function<void(FrameParameterProviderContext&)>;

class FrameRuntimeParameterCompiler {
public:
    static void registerProvider(
        std::string providerId,
        FrameParameterProvider provider);
    static bool hasProvider(const std::string& providerId);
    static void populate(
        SceneRendererComponents& state,
        Tasrovy::Render::Scene& scene,
        Tasrovy::Render::Camera& camera,
        const ViewFrameData& viewFrame,
        const std::vector<Tasrovy::Render::FramePassPacket*>& passes,
        uint32_t displayWidth,
        uint32_t displayHeight,
        std::unordered_map<
            const Tasrovy::Render::Object*,
            Tasrovy::Base::TSMat4f>& currentModelMatrices);
};

} // namespace Tasrovy::Renderer
