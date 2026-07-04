#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace Tasrovy {

class Logger {
public:
    static void Init();
    static void Shutdown();

    static std::shared_ptr<spdlog::logger>& GetLogger();

private:
    static std::shared_ptr<spdlog::logger> s_Logger;
};

}

#define LOG_TRACE(...)    ::Tasrovy::Logger::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    ::Tasrovy::Logger::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)     ::Tasrovy::Logger::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)     ::Tasrovy::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::Tasrovy::Logger::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::Tasrovy::Logger::GetLogger()->critical(__VA_ARGS__)
