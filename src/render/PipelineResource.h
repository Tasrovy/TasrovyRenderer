#pragma once

#include <cstdint>
#include <string>

namespace Tasrovy::Render {

enum class PipelineTextureFormat {
    RGBA8Unorm,
    RGBA16Float,
    RG16Float,
    Depth32Float,
    Swapchain
};

enum class PipelineTextureExtent {
    RenderRelative,
    Fixed
};

struct PipelineTextureDesc {
    std::string name;
    PipelineTextureFormat format = PipelineTextureFormat::RGBA8Unorm;
    PipelineTextureExtent extent = PipelineTextureExtent::RenderRelative;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    bool external = false;
};

enum class AttachmentLoad {
    Clear,
    Load,
    Discard
};

enum class AttachmentStore {
    Store,
    Discard
};

struct SampledTextureInput {
    std::string slot;
    std::string resource;
};

struct ColorAttachmentRef {
    std::string resource;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
};

struct DepthAttachmentRef {
    std::string resource;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    bool readOnly = false;
    float clearDepth = 1.0f;
};

} // namespace Tasrovy::Render
