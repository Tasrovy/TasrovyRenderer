#pragma once

#include "CommandList.h"
#include "Buffer.h"
#include "Descriptor.h"
#include "Image.h"
#include "Pipeline.h"
#include "ResourceBackend.h"

namespace Tasrovy::RHI {

// Narrow bridge available only to selected backend implementation files.
// Renderer and Render code never use native handles.
class BackendAccess {
public:
    static void attachCommandBuffer(CommandList& list, uint64_t handle) {
        list.useNativeCommandBuffer(handle);
    }
    static uint64_t commandBuffer(const CommandList& list) {
        return list.getNativeCommandBuffer();
    }
    static uint64_t image(const Image& value) {
        return value.backend().nativeImage();
    }
    static uint64_t imageView(const Image& value) {
        return value.backend().nativeView();
    }
    static uint64_t buffer(const Buffer& value) {
        return value.backend().nativeHandle();
    }
    static DescriptorBufferInfo descriptorInfo(const Buffer& value) {
        return value.getDescriptorInfo();
    }
    static DescriptorImageInfo descriptorInfo(const Image& value) {
        return value.getDescriptorInfo();
    }
    static DescriptorImageInfo storageDescriptorInfo(const Image& value) {
        return value.getDescriptorInfoForStorage();
    }
    static uint64_t pipeline(const Pipeline& value) {
        return value.backend().nativePipeline();
    }
    static uint64_t pipelineLayout(const Pipeline& value) {
        return value.backend().nativeLayout();
    }
    static uint64_t descriptorSet(const DescriptorSet& value) {
        return value.backend().nativeSet();
    }
    static uint64_t descriptorSetLayout(const DescriptorSetLayout& value) {
        return value.backend().nativeLayout();
    }
};

} // namespace Tasrovy::RHI
