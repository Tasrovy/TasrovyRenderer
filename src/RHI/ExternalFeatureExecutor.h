#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Tasrovy::RHI {

class CommandList;
class Device;
class Image;

// API-independent payload for an SDK-owned rendering operation. Resources are
// already resolved for the current frame. A backend that returns false from
// tryExecute() must not record commands or change resource state, so the normal
// raster/compute fallback can execute safely.
struct ExternalFeatureExecuteContext {
    uint32_t feature = 0;
    uint64_t frameNumber = 0;
    uint32_t frameIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    CommandList* commandList = nullptr;
    std::shared_ptr<Image> inputColor;
    std::shared_ptr<Image> motionVectors;
    std::shared_ptr<Image> depth;
    std::shared_ptr<Image> outputColor;
    // Prepared contract: SDR sRGB-encoded RGBA8 color, previous-minus-current
    // RG16F motion in pixel units, and conventional 0-near/1-far R32F depth.
    float motionVectorScaleX = 1.0f;
    float motionVectorScaleY = 1.0f;
    bool depthInverted = false;
    std::span<const std::byte> parameters;
};

class IExternalFeatureExecutor {
public:
    virtual ~IExternalFeatureExecutor() = default;
    // Explicitly releases SDK-owned feature state when the feature is disabled
    // or the backend is shutting down. Callers must first drain in-flight work.
    virtual void invalidateResources() noexcept {}
    // Gives an extent-bound SDK feature a chance to release before resources
    // for a different output size are allocated. A matching extent must keep
    // the existing feature alive across ordinary RenderGraph rebuilds.
    virtual void prepareForExtent(
        uint32_t width, uint32_t height) noexcept {}
    virtual bool tryExecute(
        const ExternalFeatureExecuteContext& context) noexcept = 0;
};

// Backend-selected optional SDK integration. A null result means that the
// normal RenderGraph pass should remain active as the fallback implementation.
std::unique_ptr<IExternalFeatureExecutor> createExternalFeatureExecutor(
    Device& device);

} // namespace Tasrovy::RHI
