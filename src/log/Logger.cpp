#include "Logger.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Tasrovy::Log {

std::shared_ptr<spdlog::logger> Logger::s_Logger = nullptr;

void Logger::Init() {
    if (s_Logger) return;

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("%^[%T.%e] [%l] %v%$");

    s_Logger = std::make_shared<spdlog::logger>("Tasrovy", consoleSink);
    s_Logger->set_level(spdlog::level::trace);
    s_Logger->flush_on(spdlog::level::trace);

    spdlog::set_default_logger(s_Logger);
} // namespace Tasrovy::Log

void Logger::Shutdown() {
    if (s_Logger) {
        s_Logger->flush();
        spdlog::drop(s_Logger->name());
        s_Logger = nullptr;
    }
}

std::shared_ptr<spdlog::logger>& Logger::GetLogger() {
    return s_Logger;
}

}
