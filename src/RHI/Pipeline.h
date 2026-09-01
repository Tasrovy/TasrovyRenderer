#pragma once

#include <cstdint>
#include <memory>

namespace Tasrovy::RHI {

class CommandList;
class BackendAccess;
class IPipelineBackend;

class Pipeline : public std::enable_shared_from_this<Pipeline> {
public:
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

private:
    friend class Device;
    friend class CommandList;
    friend class BackendAccess;
    Pipeline() = default;
    static std::shared_ptr<Pipeline> CreateFromBackend(
        std::unique_ptr<IPipelineBackend> backend);
    IPipelineBackend& backend();
    const IPipelineBackend& backend() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
