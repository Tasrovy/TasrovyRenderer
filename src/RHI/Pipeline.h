#pragma once

#include <cstdint>
#include <memory>

class VulkanPipeline;

namespace Tasrovy::RHI {

class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    uint64_t getNativePipeline() const;
    uint64_t getNativeLayout() const;

private:
    friend class Device;
    Pipeline() = default;
    static std::shared_ptr<Pipeline> CreateFromNative(void* nativePipeline);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
