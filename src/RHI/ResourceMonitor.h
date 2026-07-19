#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Tasrovy::RHI {

class ResourceMonitor {
public:
    ResourceMonitor();
    ~ResourceMonitor();

    ResourceMonitor(const ResourceMonitor&) = delete;
    ResourceMonitor& operator=(const ResourceMonitor&) = delete;

    void draw(
        size_t deferredDeletionCount,
        const std::vector<std::pair<std::string, double>>& gpuPassTimings);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI
