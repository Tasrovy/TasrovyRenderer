#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Tasrovy::RHI {

enum class TrackedResourceKind : uint8_t {
    Buffer,
    Image,
    Pipeline,
    DescriptorPool,
    DescriptorSetLayout,
    Count
};

struct TrackedResourceStats {
    uint64_t liveCount = 0;
    uint64_t totalCreated = 0;
    uint64_t totalDestroyed = 0;
    uint64_t liveBytes = 0;
    uint64_t peakBytes = 0;
};

struct ResourceTrackerSnapshot {
    std::array<TrackedResourceStats, static_cast<size_t>(TrackedResourceKind::Count)> resources{};
    uint64_t totalLiveCount = 0;
    uint64_t totalLiveBytes = 0;
};

class ResourceTracker {
public:
    static void created(TrackedResourceKind kind, uint64_t bytes = 0) noexcept {
        const auto index = static_cast<size_t>(kind);
        liveCounts_[index].fetch_add(1, std::memory_order_relaxed);
        totalCreated_[index].fetch_add(1, std::memory_order_relaxed);
        const uint64_t liveBytes = liveBytes_[index].fetch_add(bytes, std::memory_order_relaxed) + bytes;

        uint64_t peak = peakBytes_[index].load(std::memory_order_relaxed);
        while (liveBytes > peak &&
               !peakBytes_[index].compare_exchange_weak(
                   peak, liveBytes, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    static void destroyed(TrackedResourceKind kind, uint64_t bytes = 0) noexcept {
        const auto index = static_cast<size_t>(kind);
        liveCounts_[index].fetch_sub(1, std::memory_order_relaxed);
        totalDestroyed_[index].fetch_add(1, std::memory_order_relaxed);
        liveBytes_[index].fetch_sub(bytes, std::memory_order_relaxed);
    }

    static ResourceTrackerSnapshot snapshot() noexcept {
        ResourceTrackerSnapshot result;
        for (size_t i = 0; i < result.resources.size(); ++i) {
            auto& resource = result.resources[i];
            resource.liveCount = liveCounts_[i].load(std::memory_order_relaxed);
            resource.totalCreated = totalCreated_[i].load(std::memory_order_relaxed);
            resource.totalDestroyed = totalDestroyed_[i].load(std::memory_order_relaxed);
            resource.liveBytes = liveBytes_[i].load(std::memory_order_relaxed);
            resource.peakBytes = peakBytes_[i].load(std::memory_order_relaxed);
            result.totalLiveCount += resource.liveCount;
            result.totalLiveBytes += resource.liveBytes;
        }
        return result;
    }

    static const char* name(TrackedResourceKind kind) noexcept {
        switch (kind) {
        case TrackedResourceKind::Buffer: return "Buffer";
        case TrackedResourceKind::Image: return "Image";
        case TrackedResourceKind::Pipeline: return "Pipeline";
        case TrackedResourceKind::DescriptorPool: return "Descriptor Pool";
        case TrackedResourceKind::DescriptorSetLayout: return "Descriptor Layout";
        case TrackedResourceKind::Count: break;
        }
        return "Unknown";
    }

private:
    static constexpr size_t ResourceKindCount = static_cast<size_t>(TrackedResourceKind::Count);
    inline static std::array<std::atomic<uint64_t>, ResourceKindCount> liveCounts_{};
    inline static std::array<std::atomic<uint64_t>, ResourceKindCount> totalCreated_{};
    inline static std::array<std::atomic<uint64_t>, ResourceKindCount> totalDestroyed_{};
    inline static std::array<std::atomic<uint64_t>, ResourceKindCount> liveBytes_{};
    inline static std::array<std::atomic<uint64_t>, ResourceKindCount> peakBytes_{};
};

} // namespace Tasrovy::RHI
