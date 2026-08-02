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
    InternalRelative,
    DisplayRelative,
    Fixed
};

struct PipelineTextureDesc {
    std::string name;
    PipelineTextureFormat format = PipelineTextureFormat::RGBA8Unorm;
    PipelineTextureExtent extent = PipelineTextureExtent::InternalRelative;
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
    uint32_t binding = 0;
    bool previousFrame = false;
    // Required when a current-frame resource has multiple writers. Identifies
    // the logical resource version consumed by this input.
    std::string producerPass;
};

struct PipelineBufferDesc {
    std::string name;
    uint64_t byteSize = 0;
    // API-independent bit set translated by the selected RHI backend.
    uint32_t usageFlags = 0;
    bool hostVisible = false;
    bool external = false;
};

inline constexpr uint32_t PipelineBufferUsageTransferSource = 1u << 0u;
inline constexpr uint32_t PipelineBufferUsageVertex = 1u << 1u;
inline constexpr uint32_t PipelineBufferUsageIndex = 1u << 2u;
inline constexpr uint32_t PipelineBufferUsageUniform = 1u << 3u;
inline constexpr uint32_t PipelineBufferUsageStorage = 1u << 4u;
inline constexpr uint32_t PipelineBufferUsageIndirect = 1u << 5u;

enum class PipelineResourceAccess {
    SampledRead,
    StorageRead,
    StorageWrite,
    ColorRead,
    ColorWrite,
    DepthRead,
    DepthWrite,
    BufferTransferRead,
    BufferTransferWrite,
    BufferStorageRead,
    BufferStorageWrite
};

inline bool isBufferAccess(PipelineResourceAccess access) {
    return access == PipelineResourceAccess::BufferTransferRead ||
        access == PipelineResourceAccess::BufferTransferWrite ||
        access == PipelineResourceAccess::BufferStorageRead ||
        access == PipelineResourceAccess::BufferStorageWrite;
}

struct PipelineResourceRef {
    std::string slot;
    std::string resource;
    uint32_t binding = 0;
    PipelineResourceAccess access = PipelineResourceAccess::SampledRead;
    bool previousFrame = false;
    std::string producerPass;
};

struct ColorAttachmentRef {
    std::string resource;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    // For Load attachments, identifies the pass that produced the version
    // being preserved before this pass writes the next version.
    std::string producerPass;
};

struct DepthAttachmentRef {
    std::string resource;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    bool readOnly = false;
    float clearDepth = 1.0f;
    std::string producerPass;
};

} // namespace Tasrovy::Render
