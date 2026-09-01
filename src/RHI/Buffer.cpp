#include "Buffer.h"
#include "ResourceBackend.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI {

struct Buffer::Impl {
    std::unique_ptr<IBufferBackend> backend;
};

Buffer::~Buffer() = default;

std::shared_ptr<Buffer> Buffer::CreateFromBackend(
    std::unique_ptr<IBufferBackend> backend) {
    if (!backend) throw std::invalid_argument("Buffer backend is null");
    auto buffer = std::shared_ptr<Buffer>(new Buffer());
    buffer->impl_ = std::make_unique<Impl>();
    buffer->impl_->backend = std::move(backend);
    return buffer;
}

IBufferBackend& Buffer::backend() { return *impl_->backend; }
const IBufferBackend& Buffer::backend() const { return *impl_->backend; }
uint64_t Buffer::getSize() const { return backend().size(); }
void Buffer::setData(const void* data, uint64_t size) {
    backend().setData(data, size, 0);
}
void Buffer::setData(const void* data, uint64_t size, uint64_t offset) {
    backend().setData(data, size, offset);
}
void* Buffer::getMappedMemory() { return backend().mappedMemory(); }
DescriptorBufferInfo Buffer::getDescriptorInfo() const {
    return backend().descriptorInfo();
}

} // namespace Tasrovy::RHI
