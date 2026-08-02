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

namespace Tasrovy::RHI {

// --- RHI forward declarations ---

class Buffer;
class Pipeline;
class DescriptorSetLayout;
class DescriptorPool;
class CommandList;
class FrameScheduler;

// --- API-agnostic descriptors ---
constexpr uint32_t FormatRGBA8Unorm = 37;
constexpr uint32_t FormatRGBA8Srgb = 43;
constexpr uint32_t FormatRGBA16Float = 97;
constexpr uint32_t FormatRG16Float = 83;
constexpr uint32_t FormatDepth32Float = 126;
constexpr uint32_t FormatRGB32Float = 106;
constexpr uint32_t FormatRG32Float = 103;
constexpr uint32_t ShaderStageVertex = 0x00000001;
constexpr uint32_t ShaderStageFragment = 0x00000010;
constexpr uint32_t ShaderStageCompute = 0x00000020;
constexpr uint32_t BufferUsageVertex = 0x1;
constexpr uint32_t BufferUsageIndex = 0x2;
constexpr uint32_t BufferUsageTransferSource = 0x4;
constexpr uint32_t BufferUsageUniform = 0x10;
constexpr uint32_t BufferUsageStorage = 0x20;
constexpr uint32_t BufferUsageIndirect = 0x40;
constexpr uint32_t PrimitiveTriangleList = 3;
constexpr uint32_t CullNone = 0;
constexpr uint32_t CullFront = 0x00000001;
constexpr uint32_t CullBack = 0x00000002;
constexpr uint32_t FrontFaceClockwise = 0;
constexpr uint32_t FrontFaceCounterClockwise = 1;
constexpr uint32_t CompareLess = 1;
constexpr uint32_t CompareEqual = 2;
constexpr uint32_t CompareLessOrEqual = 3;
constexpr uint32_t CompareGreater = 4;
constexpr uint32_t CompareNotEqual = 5;
constexpr uint32_t BlendOff = 0;
constexpr uint32_t BlendAlpha = 1;
constexpr uint32_t BlendAdditive = 2;

struct BufferDesc {
    uint64_t size = 0;
    uint32_t usageFlags = 0;
    bool hostVisible = false;
};

struct ImageDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
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

struct NativeOverlayContext {
    uint64_t instance = 0;
    uint64_t physicalDevice = 0;
    uint64_t device = 0;
    uint64_t graphicsQueue = 0;
    uint32_t queueFamily = 0;
    uint32_t minImageCount = 0;
    uint32_t imageCount = 0;
    uint32_t sampleCount = 1;
    uint32_t colorFormat = 0;
};

struct VirtualShadowMapDesc {
    std::string name;
    uint32_t atlasSize = 0;
    uint32_t pageSize = 0;
    uint32_t residentPageCount = 0;
    uint32_t format = FormatDepth32Float;
};

struct PipelineDesc {
    std::string vertShaderPath;
    std::string fragShaderPath;
    std::string vertEntryPoint = "VSMain";
    std::string fragEntryPoint = "PSMain";
    uint32_t vertexStride = 0;
    std::vector<uint32_t> attributeLocations;
    std::vector<uint32_t> attributeFormats;
    std::vector<uint32_t> attributeOffsets;
    uint32_t topology = 3;
    uint32_t cullMode = 2;
    uint32_t frontFace = FrontFaceClockwise;
    bool depthTest = true;
    bool depthWrite = true;
    uint32_t depthCompareOp = 1;
    uint32_t blendMode = BlendOff;
    bool useMSAA = false;
    std::vector<uint32_t> colorAttachmentFormats;
    uint32_t depthAttachmentFormat = 0;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
};

struct ComputePipelineDesc {
    std::string shaderPath;
    std::string entryPoint = "CSMain";
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
    std::vector<uint32_t> stageFlags;
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

    static std::shared_ptr<Device> create(void* nativeContext = nullptr,
                                          void* nativeSubmitter = nullptr);
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
        uint32_t format);
    std::shared_ptr<Image> createAttachment(uint32_t width, uint32_t height, uint32_t format);
    std::shared_ptr<Image> createImage2D(uint32_t width, uint32_t height, uint32_t format);
    std::shared_ptr<Image> createRenderTexture(const RenderTextureDesc& desc);
    std::shared_ptr<Image> createVirtualShadowMap(
        const VirtualShadowMapDesc& desc);
    uint32_t resolveRenderTextureFormat(RenderTextureFormat format) const;

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
    void updateDescriptorSet(void* descriptorSet, const std::vector<DescriptorWriteDesc>& writes);
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
    uint32_t getDepthFormat() const;
    size_t getDeferredDeletionCount() const;

    NativeOverlayContext getNativeOverlayContext() const;

private:
    void retainResourceUntyped(ResourceScope scope, std::shared_ptr<void> resource);
    Device() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
