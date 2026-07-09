#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Tasrovy::RHI {

class DescriptorSetLayout : public std::enable_shared_from_this<DescriptorSetLayout> {
public:
    ~DescriptorSetLayout();
    uint64_t getNativeLayout() const;

private:
    friend class Device;
    friend class DescriptorPool;
    DescriptorSetLayout() = default;
    static std::shared_ptr<DescriptorSetLayout> CreateFromNative(void* nativeLayout);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DescriptorSet {
public:
    explicit DescriptorSet(void* nativeSet = nullptr) : nativeSet_(nativeSet) {}
    void* getNativeSet() const { return nativeSet_; }

private:
    void* nativeSet_ = nullptr;
};

class DescriptorPool : public std::enable_shared_from_this<DescriptorPool> {
public:
    ~DescriptorPool();
    DescriptorSet allocateSet(const DescriptorSetLayout& layout);

private:
    friend class Device;
    DescriptorPool() = default;
    static std::shared_ptr<DescriptorPool> CreateFromNative(void* nativePool);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
