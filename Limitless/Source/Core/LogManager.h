#pragma once

#include "Logger.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>

namespace Limitless
{
    // Forward declaration of global logger
    extern std::unique_ptr<Logger> g_Logger;

    class LogManager
    {
    public:
        static LogManager& GetInstance();
        
        // Disable copy and assignment
        LogManager(const LogManager&) = delete;
        LogManager& operator=(const LogManager&) = delete;

        // Initialize the log manager
        void Initialize(const std::string& appName = "Limitless");
        
        // Shutdown the log manager
        void Shutdown();

        // Check if the log manager is initialized
        bool IsInitialized() const { return m_Initialized; }

        // Get or create a logger
        Logger* GetLogger(const std::string& name = "default");
        
        // Get the default logger
        Logger* GetDefaultLogger() { return GetLogger("default"); }
        
        // Internal version that doesn't lock the mutex (for use within already-locked methods)
        Logger* GetDefaultLoggerInternal() { return GetLoggerInternal("default"); }

        // Global configuration
        void SetGlobalLevel(LogLevel level);
        LogLevel GetGlobalLevel() const;
        
        void SetGlobalPattern(const std::string& pattern);
        std::string GetGlobalPattern() const;

        // Global file logging control
        void EnableGlobalFileLogging(const std::string& logDir);
        void DisableGlobalFileLogging();

        // Performance monitoring
        void EnablePerformanceLogging(bool enable = true);
        bool IsPerformanceLoggingEnabled() const { return m_PerformanceLoggingEnabled; }

        // Memory monitoring
        void EnableMemoryLogging(bool enable = true);
        bool IsMemoryLoggingEnabled() const { return m_MemoryLoggingEnabled; }

        // Log system information
        void LogSystemInfo();

        // Flush all loggers
        void FlushAll();

        // Clear all loggers (useful for testing)
        void ClearAllLoggers();

        // Get logger statistics
        struct LoggerStats
        {
            std::string name;
            LogLevel level;
            size_t messageCount;
            size_t errorCount;
        };
        
        std::vector<LoggerStats> GetLoggerStats() const;

    private:
        LogManager() = default;
        ~LogManager() = default;

        std::unordered_map<std::string, std::unique_ptr<Logger>> m_Loggers;
        std::string m_AppName;
        LogLevel m_GlobalLevel = LogLevel::Info;
        std::string m_GlobalPattern;
        std::string m_LogDirectory;
        bool m_FileLoggingEnabled = false;
        bool m_PerformanceLoggingEnabled = false;
        bool m_MemoryLoggingEnabled = false;
        mutable std::mutex m_Mutex;
        bool m_Initialized = false;

        void ApplyGlobalSettings(Logger* logger);
        
        // Internal version that doesn't lock the mutex (for use within already-locked methods)
        Logger* GetLoggerInternal(const std::string& name);
        
        // Internal version that doesn't lock the mutex (for use within already-locked methods)
        void FlushAllInternal();
    };

    // Convenience functions
    inline Logger* GetLogger(const std::string& name = "default")
    {
        return LogManager::GetInstance().GetLogger(name);
    }

    inline Logger* GetDefaultLogger()
    {
        return LogManager::GetInstance().GetDefaultLogger();
    }

    // Global logging macros that use the log manager
    #define LT_LOG_TRACE(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Trace(__VA_ARGS__)
    
    #define LT_LOG_DEBUG(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Debug(__VA_ARGS__)
    
    #define LT_LOG_INFO(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Info(__VA_ARGS__)
    
    #define LT_LOG_WARN(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Warn(__VA_ARGS__)
    
    #define LT_LOG_ERROR(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Error(__VA_ARGS__)
    
    #define LT_LOG_CRITICAL(logger, ...) \
        if(auto* log = Limitless::GetLogger(logger)) log->Critical(__VA_ARGS__)

    // Default logger macros
    #define LT_LOG_TRACE_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Trace(__VA_ARGS__)
    
    #define LT_LOG_DEBUG_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Debug(__VA_ARGS__)
    
    #define LT_LOG_INFO_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Info(__VA_ARGS__)
    
    #define LT_LOG_WARN_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Warn(__VA_ARGS__)
    
    #define LT_LOG_ERROR_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Error(__VA_ARGS__)
    
    #define LT_LOG_CRITICAL_DEFAULT(...) \
        if(auto* log = Limitless::GetDefaultLogger()) log->Critical(__VA_ARGS__)
}