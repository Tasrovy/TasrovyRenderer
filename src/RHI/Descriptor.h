#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Tasrovy::RHI {

class CommandList;
class BackendAccess;
class IDescriptorPoolBackend;
class IDescriptorSetBackend;
class IDescriptorSetLayoutBackend;

class DescriptorSetLayout : public std::enable_shared_from_this<DescriptorSetLayout> {
public:
    ~DescriptorSetLayout();
private:
    friend class Device;
    friend class DescriptorPool;
    friend class CommandList;
    friend class BackendAccess;
    DescriptorSetLayout() = default;
    static std::shared_ptr<DescriptorSetLayout> CreateFromBackend(
        std::unique_ptr<IDescriptorSetLayoutBackend> backend);
    IDescriptorSetLayoutBackend& backend();
    const IDescriptorSetLayoutBackend& backend() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class DescriptorSet {
public:
    DescriptorSet() = default;

private:
    friend class Device;
    friend class CommandList;
    friend class DescriptorPool;
    friend class BackendAccess;
    explicit DescriptorSet(std::shared_ptr<IDescriptorSetBackend> backend)
        : backend_(std::move(backend)) {}
    IDescriptorSetBackend& backend() const;
    std::shared_ptr<IDescriptorSetBackend> backend_;
};

class DescriptorPool : public std::enable_shared_from_this<DescriptorPool> {
public:
    ~DescriptorPool();
    DescriptorSet allocateSet(const DescriptorSetLayout& layout);

private:
    friend class Device;
    DescriptorPool() = default;
    static std::shared_ptr<DescriptorPool> CreateFromBackend(
        std::unique_ptr<IDescriptorPoolBackend> backend);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
