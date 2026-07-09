#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include "Image.h"
#include "Pipeline.h"
#include "Descriptor.h"
#include "Pass.h"

namespace Tasrovy::UI { class UIOverlay; }
namespace Tasrovy::Windowing { class Window; }

namespace Tasrovy {
namespace Render { class Material; }
}

namespace Tasrovy::RHI {

// --- RHI forward declarations ---

class Buffer;
class Pipeline;
class DescriptorSetLayout;
class DescriptorPool;
class ReflectionBridge;
class CommandList;

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
constexpr uint32_t PrimitiveTriangleList = 3;
constexpr uint32_t CullNone = 0;
constexpr uint32_t CullFront = 0x00000001;
constexpr uint32_t CullBack = 0x00000002;
constexpr uint32_t FrontFaceCounterClockwise = 1;
constexpr uint32_t CompareLess = 1;
constexpr uint32_t CompareLessOrEqual = 3;

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
    uint32_t frontFace = 1;
    bool depthTest = true;
    bool depthWrite = true;
    uint32_t depthCompareOp = 1;
    bool useMSAA = false;
    std::vector<uint32_t> colorAttachmentFormats;
    uint32_t depthAttachmentFormat = 0;
    std::shared_ptr<DescriptorSetLayout> descriptorSetLayout;
};

enum class DescriptorResourceType : uint32_t {
    UniformBuffer,
    CombinedImageSampler,
    StorageImage
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
    static std::shared_ptr<Device> create(void* nativeContext = nullptr,
                                          void* nativeSubmitter = nullptr);
    static std::shared_ptr<Device> createForWindow(Tasrovy::Windowing::Window& window, uint32_t maxFramesInFlight = 2);
    ~Device();

    // --- Buffers ---
    std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc);
    std::shared_ptr<Buffer> createVertexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createIndexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createUniformBuffer(uint64_t size);
    std::shared_ptr<Buffer> createStagingBuffer(uint64_t size);

    // --- Images ---
    std::shared_ptr<Image> createTexture(const std::string& path, bool generateMips = true, uint32_t format = 0);
    std::shared_ptr<Image> createCubemap(const std::string& directoryPath);
    std::shared_ptr<Image> createAttachment(uint32_t width, uint32_t height, uint32_t format);
    std::shared_ptr<Image> createImage2D(uint32_t width, uint32_t height, uint32_t format);
    std::shared_ptr<Image> createRenderTexture(const RenderTextureDesc& desc);
    uint32_t resolveRenderTextureFormat(RenderTextureFormat format) const;

    // --- Pipelines ---
    std::shared_ptr<Pipeline> createGraphicsPipeline(const PipelineDesc& desc);
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
    void createIBLMaps(Image& skybox, const std::string& name);

    // --- Copy helpers ---
    void uploadBuffer(Buffer& dst, const void* data, uint64_t size);

    // --- Shader Reflection ---
    std::shared_ptr<ReflectionBridge> getReflectionBridge();
    void requestShaderReflection(Tasrovy::Render::Material* material);
    void processReflectionResults();

    // --- Frame lifecycle ---
    void waitIdle();
    void handleResize(Tasrovy::Windowing::Window& window);
    void checkSwapchain();
    uint32_t getCurrentFrameIndex() const;
    uint32_t getSwapchainWidth() const;
    uint32_t getSwapchainHeight() const;
    uint32_t getSwapchainColorFormat() const;
    uint32_t getDepthFormat() const;
    bool beginFrame(CommandList& commandList);
    void beginFrameRenderPass(CommandList& commandList);
    void endFrameRenderPass(CommandList& commandList);
    void endFrame();

    // --- UI overlay ---
    std::unique_ptr<Tasrovy::UI::UIOverlay> createUIOverlay(Tasrovy::Windowing::Window& window);
    bool beginUIFrame(Tasrovy::UI::UIOverlay& ui);
    void renderUI(Tasrovy::UI::UIOverlay& ui, CommandList& commandList);

private:
    Device() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
