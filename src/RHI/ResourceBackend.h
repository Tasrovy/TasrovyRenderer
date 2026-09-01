#pragma once

#include "RHITypes.h"

#include <cstdint>
#include <memory>

namespace Tasrovy::RHI {

class IBufferBackend {
public:
    virtual ~IBufferBackend() = default;
    virtual uint64_t size() const = 0;
    virtual void setData(const void* data, uint64_t size, uint64_t offset) = 0;
    virtual void* mappedMemory() = 0;
    virtual DescriptorBufferInfo descriptorInfo() const = 0;
    virtual uint64_t nativeHandle() const = 0;
};

class IImageBackend {
public:
    virtual ~IImageBackend() = default;
    virtual Format format() const = 0;
    virtual uint32_t mipLevels() const = 0;
    virtual DescriptorImageInfo descriptorInfo() const = 0;
    virtual DescriptorImageInfo storageDescriptorInfo() const = 0;
    virtual uint64_t nativeImage() const = 0;
    virtual uint64_t nativeView() const = 0;
    virtual uint64_t nativeSampler() const = 0;
};

class IPipelineBackend {
public:
    virtual ~IPipelineBackend() = default;
    virtual uint64_t nativePipeline() const = 0;
    virtual uint64_t nativeLayout() const = 0;
};

class IDescriptorSetLayoutBackend {
public:
    virtual ~IDescriptorSetLayoutBackend() = default;
    virtual uint64_t nativeLayout() const = 0;
};

class IDescriptorSetBackend {
public:
    virtual ~IDescriptorSetBackend() = default;
    virtual uint64_t nativeSet() const = 0;
};

class IDescriptorPoolBackend {
public:
    virtual ~IDescriptorPoolBackend() = default;
    virtual std::shared_ptr<IDescriptorSetBackend> allocateSet(
        const IDescriptorSetLayoutBackend& layout) = 0;
};

} // namespace Tasrovy::RHI
