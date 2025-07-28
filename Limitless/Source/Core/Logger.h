#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace Limitless
{
    enum class LogLevel
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Critical = 5
    };

    class Logger
    {
    public:
        Logger(const std::string& name = "Limitless");
        ~Logger() = default;

        // Disable copy constructor and assignment
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        // Enable move constructor and assignment
        Logger(Logger&&) = default;
        Logger& operator=(Logger&&) = default;

        // Logging methods
        void Trace(const std::string& message);
        void Debug(const std::string& message);
        void Info(const std::string& message);
        void Warn(const std::string& message);
        void Error(const std::string& message);
        void Critical(const std::string& message);

        // Convenience methods for common logging patterns
        void LogSystemInfo();
        void LogPerformance(const std::string& operation, double durationMs);
        void LogMemoryUsage(size_t bytes, const std::string& context = "");

        // Configuration
        void SetLevel(LogLevel level);
        LogLevel GetLevel() const;
        void SetPattern(const std::string& pattern);
        // void GetPattern() const; // Removed - spdlog::logger doesn't have this method
        
        // File logging control
        void EnableFileLogging(const std::string& filename, bool daily = true, bool rotating = false);
        void DisableFileLogging();
        
        // Utility
        void Flush();

        // Get underlying spdlog logger
        std::shared_ptr<spdlog::logger> GetSpdLogger() const 
        { 
            if (m_Logger && m_Logger.get())
            {
                return m_Logger;
            }
            return nullptr;
        }

    private:
        std::shared_ptr<spdlog::logger> m_Logger;
        std::vector<std::shared_ptr<spdlog::sinks::sink>> m_Sinks;
        
        void SetupDefaultSinks();
        void SetupFileSinks(const std::string& filename, bool daily, bool rotating);
    };

    // Global logger instance
    extern std::unique_ptr<Logger> g_Logger;

    // Convenience macros for logging
    #define LT_TRACE(...)    if(Limitless::g_Logger) Limitless::g_Logger->Trace(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_DEBUG(...)    if(Limitless::g_Logger) Limitless::g_Logger->Debug(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_INFO(...)     if(Limitless::g_Logger) Limitless::g_Logger->Info(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_WARN(...)     if(Limitless::g_Logger) Limitless::g_Logger->Warn(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_ERROR(...)    if(Limitless::g_Logger) Limitless::g_Logger->Error(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_CRITICAL(...) if(Limitless::g_Logger) Limitless::g_Logger->Critical(spdlog::fmt_lib::format(__VA_ARGS__))

    // Conditional logging macros
    #define LT_TRACE_IF(condition, ...) if(condition && Limitless::g_Logger) Limitless::g_Logger->Trace(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_DEBUG_IF(condition, ...) if(condition && Limitless::g_Logger) Limitless::g_Logger->Debug(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_INFO_IF(condition, ...)  if(condition && Limitless::g_Logger) Limitless::g_Logger->Info(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_WARN_IF(condition, ...)  if(condition && Limitless::g_Logger) Limitless::g_Logger->Warn(spdlog::fmt_lib::format(__VA_ARGS__))
    #define LT_ERROR_IF(condition, ...) if(condition && Limitless::g_Logger) Limitless::g_Logger->Error(spdlog::fmt_lib::format(__VA_ARGS__))
}