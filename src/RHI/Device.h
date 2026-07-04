#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace Tasrovy {

// --- RHI forward declarations ---

class Buffer;
class Image;
class Pipeline;
class DescriptorSetLayout;
class DescriptorPool;

// --- API-agnostic descriptors ---

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

struct PipelineDesc {
    std::string vertShaderPath;
    std::string fragShaderPath;
    std::string vertEntryPoint = "VSMain";
    std::string fragEntryPoint = "PSMain";
    bool depthTest = true;
    bool depthWrite = true;
    bool useMSAA = false;
};

struct DescriptorSetDesc {
    std::vector<uint32_t> bindingTypes;
};

// --- Device ---

class Device : public std::enable_shared_from_this<Device> {
public:
    static std::shared_ptr<Device> create();

    // --- Buffers ---
    std::shared_ptr<Buffer> createBuffer(const BufferDesc& desc);
    std::shared_ptr<Buffer> createVertexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createIndexBuffer(uint64_t size);
    std::shared_ptr<Buffer> createUniformBuffer(uint64_t size);
    std::shared_ptr<Buffer> createStagingBuffer(uint64_t size);

    // --- Images ---
    std::shared_ptr<Image> createTexture(const std::string& path, bool generateMips = true);
    std::shared_ptr<Image> createCubemap(const std::string& directoryPath);
    std::shared_ptr<Image> createAttachment(uint32_t width, uint32_t height, uint32_t format);
    std::shared_ptr<Image> createImage2D(uint32_t width, uint32_t height, uint32_t format);

    // --- Pipelines ---
    std::shared_ptr<Pipeline> createGraphicsPipeline(const PipelineDesc& desc);

    // --- Descriptors ---
    std::shared_ptr<DescriptorSetLayout> createDescriptorSetLayout(
        const std::vector<uint32_t>& bindingTypes);
    std::shared_ptr<DescriptorPool> createDescriptorPool(
        uint32_t maxSets, const std::vector<uint32_t>& poolSizes);

    // --- Copy helpers ---
    void uploadBuffer(Buffer& dst, const void* data, uint64_t size);

private:
    Device() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy
