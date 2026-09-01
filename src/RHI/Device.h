#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <array>
#include "Image.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Pass.h"
#include "RHITypes.h"

namespace Tasrovy::RHI {

// --- RHI forward declarations ---

class Buffer;
class Pipeline;
class DescriptorSetLayout;
class DescriptorPool;
class CommandList;
class FrameScheduler;

// --- API-agnostic descriptors ---
struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    bool hostVisible = false;
};

struct ImageDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::Unknown;
    bool generateMipmaps = false;
    bool isCubemap = false;
};

enum class RenderTextureFormat : uint32_t {
    RGBA8Unorm,
    RGBA16Float,
    RG16Float,
    Depth32Float,
    Swapchain
};

struct RenderTextureDesc {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    RenderTextureFormat format = RenderTextureFormat::RGBA8Unorm;
    bool external = false;
    bool storage = false;
};

struct SurfaceDeviceCreateInfo {
    void* nativeWindowHandle = nullptr;
    uint32_t framebufferWidth = 0;
    uint32_t framebufferHeight = 0;
    uint32_t maxFramesInFlight = 2;
};

struct BackendInteropContext {
    GraphicsAPI api = GraphicsAPI::Unknown;
    std::array<uintptr_t, 4> handles{};
    uint32_t queueIndex = 0;
    uint32_t minImageCount = 0;
    uint32_t imageCount = 0;
    uint32_t sampleCount = 1;
    Format presentationFormat = Format::Unknown;
};

struct VirtualShadowMapDesc {
    std::string name;
    uint32_t atlasSize = 0;
    uint32_t pageSize = 0;
    uint32_t residentPageCount = 0;
    Format format = Format::Depth32Float;
};

struct ShaderModuleDesc {
    std::string sourcePath;
    std::string entryPoint;
    ShaderStage stage = ShaderStage::Vertex;
    uint64_t permutation = 0;
    bool hasPermutation = false;
};

struct PipelineDesc {
    ShaderModuleDesc vertexShader;
    ShaderModuleDesc fragmentShader;
    uint32_t vertexStride = 0;
    std::vector<uint32_t> attributeLocations;
    std::vector<Format> attributeFormats;
    std::vector<uint32_t> attributeOffsets;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::Clockwise;
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompareOp = CompareOp::Less;
    BlendMode blendMode = BlendMode::Off;
    bool useMSAA = false;
    std::vector<Format> colorAttachmentFormats;
    Format depthAttachmentFormat = Format::Unknown;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
};

struct ComputePipelineDesc {
    ShaderModuleDesc shader;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
};

enum class DescriptorResourceType : uint32_t {
    UniformBuffer,
    CombinedImageSampler,
    StorageImage,
    StorageBuffer
};

struct DescriptorSetDesc {
    std::vector<DescriptorResourceType> bindingTypes;
    std::vector<ShaderStageFlags> stageFlags;
};

struct DescriptorPoolSizeDesc {
    DescriptorResourceType type = DescriptorResourceType::UniformBuffer;
    uint32_t count = 0;
};

struct DescriptorWriteDesc {
    uint32_t binding = 0;
    DescriptorResourceType type = DescriptorResourceType::UniformBuffer;
    std::shared_ptr<Buffer> buffer;
    std::shared_ptr<Image> image;
    DescriptorImageInfo imageInfo;
};

enum class IBLMapType : uint32_t {
    Irradiance,
    Prefiltered,
    BrdfLut
};

// --- Device ---

class Device : public std::enable_shared_from_this<Device> {
public:
    using ResourceScope = uint64_t;

    static std::shared_ptr<Device> createForSurface(
        const SurfaceDeviceCreateInfo& createInfo);
    ~Device();

    ResourceScope createResourceScope();
    void resetResourceScope(ResourceScope scope);
    void destroyResourceScope(ResourceScope scope);

    template <typename T>
    std::shared_ptr<T> retainResource(ResourceScope scope, std::shared_ptr<T> resource) {
        retainResourceUntyped(scope, std::static_pointer_cast<void>(resource));
        return resource;
    }

    // --- Buffers ---
    std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc);
    std::shared_ptr<Buffer> createVertexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createIndexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createUniformBuffer(uint64_t size);
    std::shared_ptr<Buffer> createStagingBuffer(uint64_t size);
    std::shared_ptr<CommandList> createCommandList();

    // --- Images ---
    std::shared_ptr<Image> createTexture(const ImageUploadDesc& upload);
    std::shared_ptr<Image> createSolidTexture(
        const std::array<float, 4>& color,
        Format format);
    std::shared_ptr<Image> createAttachment(uint32_t width, uint32_t height, Format format);
    std::shared_ptr<Image> createImage2D(uint32_t width, uint32_t height, Format format);
    std::shared_ptr<Image> createRenderTexture(const RenderTextureDesc& desc);
    std::shared_ptr<Image> createVirtualShadowMap(
        const VirtualShadowMapDesc& desc);
    Format resolveRenderTextureFormat(RenderTextureFormat format) const;

    // --- Pipelines ---
    std::shared_ptr<Pipeline> createGraphicsPipeline(const PipelineDesc& desc);
    std::shared_ptr<Pipeline> createComputePipeline(const ComputePipelineDesc& desc);
    std::shared_ptr<Pass> createPass(PassDesc desc);

    // --- Descriptors ---
    std::shared_ptr<DescriptorSetLayout> createDescriptorSetLayout(
        const DescriptorSetDesc& desc);
    std::shared_ptr<DescriptorPool> createDescriptorPool(
        uint32_t maxSets, const std::vector<DescriptorPoolSizeDesc>& poolSizes);
    DescriptorSet allocateDescriptorSet(DescriptorPool& pool, const DescriptorSetLayout& layout);
    void updateDescriptorSet(const DescriptorSet& descriptorSet, const std::vector<DescriptorWriteDesc>& writes);
    DescriptorImageInfo getIBLDescriptorInfo(IBLMapType mapType, const std::string& name = "DefaultSky") const;

    // --- IBL ---
    // Legacy explicit path retained while IBL is dormant. Future backends
    // should generate derived environment maps automatically when needed.
    [[deprecated("IBL generation should be owned automatically by Device")]]
    void createIBLMaps(Image& skybox, const std::string& name);

    // Device owns the backend objects; FrameScheduler is the only interface
    // that performs frame acquire, synchronization, submission and present.
    FrameScheduler& getFrameScheduler();
    const FrameScheduler& getFrameScheduler() const;

    // --- Resource information ---
    Format getDepthFormat() const;
    size_t getDeferredDeletionCount() const;

    BackendInteropContext getBackendInteropContext() const;

private:
    void retainResourceUntyped(ResourceScope scope, std::shared_ptr<void> resource);
    Device() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
