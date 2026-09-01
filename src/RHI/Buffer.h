#pragma once

#include <memory>
#include <cstdint>
#include "RHITypes.h"

namespace Tasrovy::RHI {

class CommandList;
class BackendAccess;
class IBufferBackend;

class Buffer : public std::enable_shared_from_this<Buffer> {
public:
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    uint64_t getSize() const;
    void setData(const void* data, uint64_t size);
    void setData(const void* data, uint64_t size, uint64_t offset);
    void* getMappedMemory();

private:
    friend class Device;
    friend class CommandList;
    friend class BackendAccess;
    Buffer() = default;
    static std::shared_ptr<Buffer> CreateFromBackend(
        std::unique_ptr<IBufferBackend> backend);
    IBufferBackend& backend();
    const IBufferBackend& backend() const;
    DescriptorBufferInfo getDescriptorInfo() const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
