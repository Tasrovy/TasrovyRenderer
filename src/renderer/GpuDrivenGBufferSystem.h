#pragma once

#include "../RHI/Descriptor.h"
#include "../RHI/Device.h"
#include "../base/TSMatrix.h"
#include "../base/TSVector.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Tasrovy::Render {
class Material;
class Object;
}

namespace Tasrovy::RHI {
class Buffer;
class DescriptorPool;
class DescriptorSetLayout;
class Image;
class Pipeline;
}

namespace Tasrovy::Renderer {

using Tasrovy::Base::TSMat4f;
using Tasrovy::Base::TSVec3f;
using Tasrovy::Base::TSVec4f;

struct GpuDrivenFrameData {
    TSMat4f view;
    TSMat4f proj;
    TSMat4f previousView;
    TSMat4f previousProj;
    TSVec4f uvTransform;
    TSVec4f taaParams;
    uint32_t drawCount = 0;
    uint32_t padding[3]{};
};

struct GpuDrivenDrawData {
    TSMat4f model;
    TSMat4f previousModel;
    TSVec4f baseColorFactorAndTexture;
    TSVec4f materialParams;
    TSVec4f materialEmission;
    TSVec4f rimColorAndStrength;
    TSVec4f rimParams;
    TSVec4f worldBounds;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};

struct GpuIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};

struct GpuDrivenDrawSource {
    const Render::Object* object = nullptr;
    std::shared_ptr<Render::Material> material;
    std::string materialName;
    TSVec3f localBoundsCenter = TSVec3f(0.0f);
    float localBoundsRadius = 0.0f;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
};

struct GpuDrivenDrawGroup {
    std::shared_ptr<RHI::Image> baseColor;
    uint32_t firstDraw = 0;
    uint32_t drawCount = 0;
    uint32_t frontFace = RHI::FrontFaceClockwise;
    std::vector<RHI::DescriptorSet> descriptorSets;
};

struct GpuDrivenGBufferResources {
    bool ready = false;
    std::shared_ptr<RHI::Buffer> vertexBuffer;
    std::shared_ptr<RHI::Buffer> indexBuffer;
    std::vector<std::shared_ptr<RHI::Buffer>> frameBuffers;
    std::vector<std::shared_ptr<RHI::Buffer>> drawBuffers;
    std::vector<std::shared_ptr<RHI::Buffer>> indirectBuffers;
    std::vector<GpuDrivenDrawSource> draws;
    std::vector<GpuDrivenDrawGroup> groups;
    std::shared_ptr<RHI::DescriptorSetLayout> graphicsLayout;
    std::shared_ptr<RHI::DescriptorPool> graphicsPool;
    std::shared_ptr<RHI::Pipeline> graphicsPipeline;
    std::shared_ptr<RHI::DescriptorSetLayout> computeLayout;
    std::shared_ptr<RHI::DescriptorPool> computePool;
    std::vector<RHI::DescriptorSet> computeSets;
    std::shared_ptr<RHI::Pipeline> computePipeline;
};

class GpuDrivenGBufferSystem {
public:
    void reset() { resources_ = {}; }
    GpuDrivenGBufferResources& resources() { return resources_; }
    const GpuDrivenGBufferResources& resources() const { return resources_; }

private:
    GpuDrivenGBufferResources resources_;
};

static_assert(offsetof(GpuDrivenFrameData, drawCount) == 288);
static_assert(sizeof(GpuDrivenFrameData) == 304);
static_assert(offsetof(GpuDrivenDrawData, worldBounds) == 208);
static_assert(offsetof(GpuDrivenDrawData, indexCount) == 224);
static_assert(sizeof(GpuDrivenDrawData) == 240);
static_assert(sizeof(GpuIndirectCommand) == 20);

} // namespace Tasrovy::Renderer
