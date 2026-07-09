#pragma once

#include <memory>
#include <cstdint>

namespace Tasrovy::RHI {

struct DescriptorBufferInfo {
    uint64_t nativeBuffer = 0;
    uint64_t offset = 0;
    uint64_t range = 0;
};

class Buffer : public std::enable_shared_from_this<Buffer> {
public:
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    uint64_t getSize() const;
    void setData(const void* data, uint64_t size);
    void setData(const void* data, uint64_t size, uint64_t offset);
    void* getMappedMemory();

    uint64_t getNativeHandle() const;
    DescriptorBufferInfo getDescriptorInfo() const;

private:
    friend class Device;
    friend class CommandList;
    Buffer() = default;
    static std::shared_ptr<Buffer> CreateFromNative(void* nativeContext, uint64_t size, uint32_t usageFlags, bool hostVisible);
    static std::shared_ptr<Buffer> CreateStagingFromNative(void* nativeContext, uint64_t size);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
