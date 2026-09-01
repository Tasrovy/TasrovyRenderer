#include "Descriptor.h"
#include "ResourceBackend.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI {

struct DescriptorSetLayout::Impl {
    std::unique_ptr<IDescriptorSetLayoutBackend> backend;
};

struct DescriptorPool::Impl {
    std::unique_ptr<IDescriptorPoolBackend> backend;
};

DescriptorSetLayout::~DescriptorSetLayout() = default;
DescriptorPool::~DescriptorPool() = default;

std::shared_ptr<DescriptorSetLayout> DescriptorSetLayout::CreateFromBackend(
    std::unique_ptr<IDescriptorSetLayoutBackend> backend) {
    if (!backend) {
        throw std::invalid_argument("Descriptor set layout backend is null");
    }
    auto layout = std::shared_ptr<DescriptorSetLayout>(
        new DescriptorSetLayout());
    layout->impl_ = std::make_unique<Impl>();
    layout->impl_->backend = std::move(backend);
    return layout;
}

IDescriptorSetLayoutBackend& DescriptorSetLayout::backend() {
    return *impl_->backend;
}
const IDescriptorSetLayoutBackend& DescriptorSetLayout::backend() const {
    return *impl_->backend;
}

IDescriptorSetBackend& DescriptorSet::backend() const {
    if (!backend_) throw std::logic_error("Descriptor set is empty");
    return *backend_;
}

std::shared_ptr<DescriptorPool> DescriptorPool::CreateFromBackend(
    std::unique_ptr<IDescriptorPoolBackend> backend) {
    if (!backend) throw std::invalid_argument("Descriptor pool backend is null");
    auto pool = std::shared_ptr<DescriptorPool>(new DescriptorPool());
    pool->impl_ = std::make_unique<Impl>();
    pool->impl_->backend = std::move(backend);
    return pool;
}

DescriptorSet DescriptorPool::allocateSet(
    const DescriptorSetLayout& layout) {
    return DescriptorSet(
        impl_->backend->allocateSet(layout.backend()));
}

} // namespace Tasrovy::RHI
