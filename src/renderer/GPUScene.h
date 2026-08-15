#pragma once

#include "FrameParameterBuilder.h"
#include "FrameBufferUpload.h"
#include "../base/TSMatrix.h"
#include "../base/TSVector.h"
#include "../RHI/Device.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Tasrovy::RHI { class Buffer; }
namespace Tasrovy::Render {
class Camera;
class Scene;
struct FrameSourceRegistry;
}

namespace Tasrovy::Renderer {

struct ViewFrameData;
struct ViewState;

struct ViewUniform {
    TSMat4f view;
    TSMat4f projection;
    TSMat4f unflippedProjection;
    TSMat4f previousView;
    TSMat4f previousProjection;
    TSMat4f previousUnflippedProjection;
    TSVec4f cameraPositionAndNear;
    TSVec4f renderSizeAndFar;
    TSVec4f jitterAndMipBias;
};

struct ObjectData {
    TSMat4f model;
    TSMat4f previousModel;
    uint32_t materialIndex = 0;
    uint32_t flags = 0;
    uint32_t padding[2]{};
};

struct MaterialData {
    TSVec4f baseColorFactorAndTexture = TSVec4f(1.0f);
    TSVec4f surface = TSVec4f(0.0f, 1.0f, 1.0f, 0.0f);
    TSVec4f emission = TSVec4f(0.0f);
    TSVec4f rimColorAndStrength = TSVec4f(1.0f, 1.0f, 1.0f, 0.0f);
    TSVec4f rimParams = TSVec4f(3.0f, 0.0f, 0.0f, 0.0f);
    TSVec4f baseColorUvTransform = TSVec4f(1.0f, 1.0f, 0.0f, 0.0f);
    TSVec4f normalUvTransform = TSVec4f(1.0f, 1.0f, 0.0f, 0.0f);
    TSVec4f emissiveUvTransform = TSVec4f(1.0f, 1.0f, 0.0f, 0.0f);
    TSVec4f mraUvTransform = TSVec4f(1.0f, 1.0f, 0.0f, 0.0f);
    TSVec4f textureUvModes = TSVec4f(0.0f);
};

struct SceneLightData {
    TSVec4f meta = TSVec4f(0.0f);
    TSVec4f primaryDirection = TSVec4f(0.0f);
    TSVec4f primaryColor = TSVec4f(0.0f);
    std::array<GpuLightData, MaxSceneLights> lights{};
};

class GPUScene {
public:
    void reset();
    void prepare(
        RHI::Device& device,
        RHI::Device::ResourceScope scope,
        uint32_t framesInFlight,
        const Render::FrameSourceRegistry& sources);
    void buildUploads(
        uint32_t frameIndex,
        const Render::Scene& scene,
        const Render::Camera& camera,
        const ViewFrameData& viewFrame,
        const ViewState& viewState,
        const Render::FrameSourceRegistry& sources,
        const RendererSettings& settings,
        bool environmentLightingEnabled,
        uint32_t internalWidth,
        uint32_t internalHeight,
        uint32_t displayWidth,
        uint32_t displayHeight,
        std::vector<FrameBufferUpload>& uploads) const;

    const std::shared_ptr<RHI::Buffer>& viewBuffer(uint32_t frame) const;
    const std::shared_ptr<RHI::Buffer>& objectBuffer(uint32_t frame) const;
    const std::shared_ptr<RHI::Buffer>& materialBuffer(uint32_t frame) const;
    const std::shared_ptr<RHI::Buffer>& lightBuffer(uint32_t frame) const;

private:
    uint32_t objectCapacity_ = 0;
    uint32_t materialCapacity_ = 0;
    std::vector<std::shared_ptr<RHI::Buffer>> viewBuffers_;
    std::vector<std::shared_ptr<RHI::Buffer>> objectBuffers_;
    std::vector<std::shared_ptr<RHI::Buffer>> materialBuffers_;
    std::vector<std::shared_ptr<RHI::Buffer>> lightBuffers_;
};

static_assert(sizeof(ViewUniform) % 16 == 0);
static_assert(sizeof(ObjectData) == 144);
static_assert(sizeof(MaterialData) == 160);

} // namespace Tasrovy::Renderer
