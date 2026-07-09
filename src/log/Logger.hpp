#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace Tasrovy::Log {

class Logger {
public:
    static void Init();
    static void Shutdown();

    static std::shared_ptr<spdlog::logger>& GetLogger();

private:
    static std::shared_ptr<spdlog::logger> s_Logger;
};

} // namespace Tasrovy::Log

#define LOG_TRACE(...)    ::Tasrovy::Log::Logger::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    ::Tasrovy::Log::Logger::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)     ::Tasrovy::Log::Logger::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)     ::Tasrovy::Log::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::Tasrovy::Log::Logger::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::Tasrovy::Log::Logger::GetLogger()->critical(__VA_ARGS__)
