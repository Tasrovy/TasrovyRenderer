#include "ResourceMonitor.h"

#include "../RHI/ResourceTracker.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace Tasrovy::Renderer {

using Tasrovy::RHI::ResourceTrackerSnapshot;
using Tasrovy::RHI::TrackedResourceKind;
using Tasrovy::RHI::ResourceTracker;

namespace {

constexpr double SampleIntervalSeconds = 0.5;
constexpr size_t MaxHistorySamples = 240;

struct ProcessMemorySnapshot {
    bool available = false;
    uint64_t workingSetBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint32_t handleCount = 0;
};

ProcessMemorySnapshot queryProcessMemory() {
    ProcessMemorySnapshot result;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        result.available = true;
        result.workingSetBytes = static_cast<uint64_t>(counters.WorkingSetSize);
        result.peakWorkingSetBytes = static_cast<uint64_t>(counters.PeakWorkingSetSize);
        result.privateBytes = static_cast<uint64_t>(counters.PrivateUsage);
        DWORD handles = 0;
        if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
            result.handleCount = handles;
        }
    }
#endif
    return result;
}

std::string formatBytes(uint64_t bytes) {
    constexpr double KiB = 1024.0;
    constexpr double MiB = KiB * 1024.0;
    constexpr double GiB = MiB * 1024.0;
    char text[64]{};
    if (bytes >= static_cast<uint64_t>(GiB)) {
        std::snprintf(text, sizeof(text), "%.2f GiB", static_cast<double>(bytes) / GiB);
    } else if (bytes >= static_cast<uint64_t>(MiB)) {
        std::snprintf(text, sizeof(text), "%.2f MiB", static_cast<double>(bytes) / MiB);
    } else if (bytes >= static_cast<uint64_t>(KiB)) {
        std::snprintf(text, sizeof(text), "%.2f KiB", static_cast<double>(bytes) / KiB);
    } else {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

void appendHistory(std::vector<float>& values, float value) {
    if (values.size() == MaxHistorySamples) {
        values.erase(values.begin());
    }
    values.push_back(value);
}

double trendMiBPerMinute(const std::vector<float>& values) {
    const size_t sampleCount = std::min<size_t>(values.size(), 60);
    if (sampleCount < 3) {
        return 0.0;
    }

    const size_t first = values.size() - sampleCount;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const double x = static_cast<double>(i) * SampleIntervalSeconds;
        const double y = static_cast<double>(values[first + i]);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double denominator = static_cast<double>(sampleCount) * sumXX - sumX * sumX;
    if (std::abs(denominator) < 1e-9) {
        return 0.0;
    }
    const double mibPerSecond =
        (static_cast<double>(sampleCount) * sumXY - sumX * sumY) / denominator;
    return mibPerSecond * 60.0;
}

} // namespace

struct ResourceMonitor::Impl {
    using Clock = std::chrono::steady_clock;

    Clock::time_point lastSample{};
    ProcessMemorySnapshot process{};
    ResourceTrackerSnapshot tracked{};
    std::vector<float> privateHistoryMiB;
    std::vector<float> workingSetHistoryMiB;
    std::vector<float> trackedHistoryMiB;
    uint64_t baselinePrivateBytes = 0;
    uint64_t baselineTrackedBytes = 0;
    uint64_t baselineTrackedCount = 0;
    bool baselineReady = false;

    void resetBaseline() {
        baselinePrivateBytes = process.privateBytes;
        baselineTrackedBytes = tracked.totalLiveBytes;
        baselineTrackedCount = tracked.totalLiveCount;
        privateHistoryMiB.clear();
        workingSetHistoryMiB.clear();
        trackedHistoryMiB.clear();
        baselineReady = process.available;
    }

    void sampleIfNeeded() {
        const auto now = Clock::now();
        if (lastSample.time_since_epoch().count() != 0 &&
            std::chrono::duration<double>(now - lastSample).count() < SampleIntervalSeconds) {
            tracked = ResourceTracker::snapshot();
            return;
        }

        lastSample = now;
        process = queryProcessMemory();
        tracked = ResourceTracker::snapshot();
        constexpr double MiB = 1024.0 * 1024.0;
        if (process.available) {
            appendHistory(privateHistoryMiB, static_cast<float>(static_cast<double>(process.privateBytes) / MiB));
            appendHistory(workingSetHistoryMiB, static_cast<float>(static_cast<double>(process.workingSetBytes) / MiB));
        }
        appendHistory(trackedHistoryMiB, static_cast<float>(static_cast<double>(tracked.totalLiveBytes) / MiB));

        if (!baselineReady && process.available) {
            baselinePrivateBytes = process.privateBytes;
            baselineTrackedBytes = tracked.totalLiveBytes;
            baselineTrackedCount = tracked.totalLiveCount;
            baselineReady = true;
        }
    }
};

ResourceMonitor::ResourceMonitor() : impl_(std::make_unique<Impl>()) {}
ResourceMonitor::~ResourceMonitor() = default;

void ResourceMonitor::draw(
    size_t deferredDeletionCount,
    const std::vector<std::pair<std::string, double>>& gpuPassTimings) {
    auto& state = *impl_;
    state.sampleIfNeeded();

    ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Resource Monitor")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset Baseline")) {
        state.resetBaseline();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Samples every %.1f s, history %.0f s", SampleIntervalSeconds,
        SampleIntervalSeconds * static_cast<double>(MaxHistorySamples));

    ImGui::SeparatorText("Process Memory");
    if (!state.process.available) {
        ImGui::TextDisabled("Process memory counters are unavailable on this platform.");
    } else {
        ImGui::Text("Private bytes: %s", formatBytes(state.process.privateBytes).c_str());
        ImGui::Text("Working set: %s", formatBytes(state.process.workingSetBytes).c_str());
        ImGui::Text("Peak working set: %s", formatBytes(state.process.peakWorkingSetBytes).c_str());
        ImGui::Text("Process handles: %u", state.process.handleCount);

        if (!state.privateHistoryMiB.empty()) {
            ImGui::PlotLines(
                "Private MiB",
                state.privateHistoryMiB.data(),
                static_cast<int>(state.privateHistoryMiB.size()),
                0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0.0f, 70.0f));
            ImGui::PlotLines(
                "Working Set MiB",
                state.workingSetHistoryMiB.data(),
                static_cast<int>(state.workingSetHistoryMiB.size()),
                0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0.0f, 70.0f));
        }
    }

    ImGui::SeparatorText("Tracked Vulkan Resources");
    ImGui::Text("Managed GPU memory: %s", formatBytes(state.tracked.totalLiveBytes).c_str());
    ImGui::Text("Live managed resources: %llu",
        static_cast<unsigned long long>(state.tracked.totalLiveCount));
    ImGui::Text("Deferred deletions: %zu", deferredDeletionCount);
    if (!state.trackedHistoryMiB.empty()) {
        ImGui::PlotLines(
            "GPU allocations MiB",
            state.trackedHistoryMiB.data(),
            static_cast<int>(state.trackedHistoryMiB.size()),
            0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0.0f, 70.0f));
    }

    ImGui::SeparatorText("GPU Pass Timings");
    double totalGpuMilliseconds = 0.0;
    for (const auto& [name, milliseconds] : gpuPassTimings) {
        totalGpuMilliseconds += milliseconds;
        ImGui::Text("%-20s %7.3f ms", name.c_str(), milliseconds);
    }
    if (gpuPassTimings.empty()) {
        ImGui::TextDisabled("Waiting for completed timestamp queries...");
    } else {
        ImGui::Separator();
        ImGui::Text("Measured pass total: %.3f ms", totalGpuMilliseconds);
    }

    if (ImGui::BeginTable("ResourceStats", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Live");
        ImGui::TableSetupColumn("Created");
        ImGui::TableSetupColumn("Destroyed");
        ImGui::TableSetupColumn("Live / Peak");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < state.tracked.resources.size(); ++i) {
            const auto kind = static_cast<TrackedResourceKind>(i);
            const auto& stats = state.tracked.resources[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(ResourceTracker::name(kind));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(stats.liveCount));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", static_cast<unsigned long long>(stats.totalCreated));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(stats.totalDestroyed));
            ImGui::TableSetColumnIndex(4);
            if (stats.liveBytes != 0 || stats.peakBytes != 0) {
                ImGui::Text("%s / %s", formatBytes(stats.liveBytes).c_str(), formatBytes(stats.peakBytes).c_str());
            } else {
                ImGui::TextUnformatted("-");
            }
        }
        ImGui::EndTable();
    }

    const double privateTrend = trendMiBPerMinute(state.privateHistoryMiB);
    const double trackedTrend = trendMiBPerMinute(state.trackedHistoryMiB);
    const uint64_t privateGrowth = state.process.privateBytes > state.baselinePrivateBytes
        ? state.process.privateBytes - state.baselinePrivateBytes : 0;
    const uint64_t trackedGrowth = state.tracked.totalLiveBytes > state.baselineTrackedBytes
        ? state.tracked.totalLiveBytes - state.baselineTrackedBytes : 0;
    const uint64_t countGrowth = state.tracked.totalLiveCount > state.baselineTrackedCount
        ? state.tracked.totalLiveCount - state.baselineTrackedCount : 0;
    constexpr uint64_t MiB = 1024ull * 1024ull;
    const bool enoughSamples = state.privateHistoryMiB.size() >= 30;
    const bool processGrowing = enoughSamples && privateGrowth > 16 * MiB && privateTrend > 2.0;
    const bool resourcesGrowing = enoughSamples &&
        ((trackedGrowth > 8 * MiB && trackedTrend > 1.0) || countGrowth > 16);

    ImGui::SeparatorText("Leak Watch");
    ImGui::Text("Private trend: %+.2f MiB/min", privateTrend);
    ImGui::Text("Tracked GPU trend: %+.2f MiB/min", trackedTrend);
    if (!enoughSamples) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1.0f),
            "Collecting baseline (%zu / 30 samples)", state.privateHistoryMiB.size());
    } else if (processGrowing || resourcesGrowing) {
        ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.25f, 1.0f), "Suspected persistent growth");
        if (processGrowing) {
            ImGui::BulletText("Private memory grew by %s", formatBytes(privateGrowth).c_str());
        }
        if (resourcesGrowing) {
            ImGui::BulletText("Tracked resources grew by %s and %llu objects",
                formatBytes(trackedGrowth).c_str(), static_cast<unsigned long long>(countGrowth));
        }
    } else {
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "No persistent growth detected");
    }
    ImGui::TextDisabled("This is a trend detector. Confirm warnings with validation layers or a heap profiler.");

    ImGui::End();
}

} // namespace Tasrovy::Renderer
