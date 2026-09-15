#include "Logger.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <system_error>

namespace Tasrovy::Log {
namespace {

std::filesystem::path findLogDirectory(std::error_code& error) {
    auto directory = std::filesystem::current_path(error);
    if (error) return {};

    auto search = directory;
    while (!search.empty()) {
        std::error_code existsError;
        if (std::filesystem::exists(
                search / "res" / "Shaders", existsError) &&
            !existsError) {
            directory = search;
            break;
        }
        const auto parent = search.parent_path();
        if (parent == search) break;
        search = parent;
    }
    return directory / "logs";
}

} // namespace

std::shared_ptr<spdlog::logger> Logger::s_Logger = nullptr;
std::shared_ptr<spdlog::logger> Logger::s_GpuMemoryLogger = nullptr;

void Logger::Init() {
    if (s_Logger) return;

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("%^[%T.%e] [%l] %v%$");

    s_Logger = std::make_shared<spdlog::logger>("Tasrovy", consoleSink);
    s_Logger->set_level(spdlog::level::trace);
    s_Logger->flush_on(spdlog::level::trace);

    spdlog::set_default_logger(s_Logger);

    std::error_code pathError;
    const auto logDirectory = findLogDirectory(pathError);
    if (!pathError) {
        std::filesystem::create_directories(logDirectory, pathError);
    }
    if (pathError) {
        s_Logger->warn(
            "GPU memory trace disabled: cannot create log directory ({})",
            pathError.message());
        return;
    }

    const auto gpuMemoryLogPath = logDirectory / "gpu_memory.log";
    try {
        auto gpuMemorySink =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                gpuMemoryLogPath.string(), true);
        gpuMemorySink->set_pattern("[%Y-%m-%d %T.%e] [%t] %v");
        s_GpuMemoryLogger = std::make_shared<spdlog::logger>(
            "TasrovyGpuMemory", gpuMemorySink);
        s_GpuMemoryLogger->set_level(spdlog::level::trace);
        s_GpuMemoryLogger->flush_on(spdlog::level::trace);
        s_GpuMemoryLogger->info("[SESSION] GPU memory trace started");
        s_Logger->info(
            "GPU memory trace: '{}'",
            gpuMemoryLogPath.string());
    } catch (const spdlog::spdlog_ex& error) {
        s_GpuMemoryLogger = nullptr;
        s_Logger->warn(
            "GPU memory trace disabled: {}",
            error.what());
    }
} // namespace Tasrovy::Log

void Logger::Shutdown() {
    if (s_GpuMemoryLogger) {
        s_GpuMemoryLogger->info("[SESSION] GPU memory trace stopped");
        s_GpuMemoryLogger->flush();
        spdlog::drop(s_GpuMemoryLogger->name());
        s_GpuMemoryLogger = nullptr;
    }
    if (s_Logger) {
        s_Logger->flush();
        spdlog::drop(s_Logger->name());
        s_Logger = nullptr;
    }
}

std::shared_ptr<spdlog::logger>& Logger::GetLogger() {
    return s_Logger;
}

std::shared_ptr<spdlog::logger>& Logger::GetGpuMemoryLogger() {
    return s_GpuMemoryLogger;
}

}
