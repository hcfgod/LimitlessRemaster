#include "LogManager.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <chrono>
#include <iostream>

namespace Limitless
{
    // Global logger instance definition
    std::unique_ptr<Logger> g_Logger;

    LogManager& LogManager::GetInstance()
    {
        static LogManager instance;
        return instance;
    }

    void LogManager::Initialize(const std::string& appName)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (m_Initialized)
        {
            return;
        }

        m_AppName = appName;
        
        // Initialize global logger for macros FIRST - before anything else
        g_Logger = std::make_unique<Logger>("Limitless");
        
        // Verify the global logger was created successfully
        if (!g_Logger || !g_Logger->GetSpdLogger())
        {
            // If global logger creation failed, this is a critical error
            // We can't use logging macros here, so we'll use stderr
            std::cerr << "Critical: Failed to create global logger!" << std::endl;
            
            // Try to create a fallback global logger
            g_Logger = std::make_unique<Logger>("fallback");
        }
        
        // Initialize spdlog if not already done
        try
        {
            // Set up spdlog's default logger if it doesn't exist
            if (!spdlog::default_logger())
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                auto default_logger = std::make_shared<spdlog::logger>("default", console_sink);
                spdlog::set_default_logger(default_logger);
            }
        }
        catch (...)
        {
            // If spdlog initialization fails, we'll handle it in the Logger constructor
        }
        
        // Create default logger
        GetLoggerInternal("default");
        
        // Set default global pattern
        m_GlobalPattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";
        
        m_Initialized = true;
        
        // Log initialization - use GetLoggerInternal since we already have the mutex locked
        if (auto logger = GetLoggerInternal("default"))
        {
            logger->Info(spdlog::fmt_lib::format("LogManager initialized for application: {}", appName));
        }
    }

    void LogManager::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (!m_Initialized)
        {
            return;
        }

        // Log shutdown
        if (auto logger = GetDefaultLoggerInternal())
        {
            logger->Info("LogManager shutting down");
        }

        // Flush all loggers - use internal version since we already have the mutex locked
        FlushAllInternal();
        
        // Clear all loggers
        m_Loggers.clear();
        
        // Clean up global logger
        g_Logger.reset();
        
        m_Initialized = false;
    }

    Logger* LogManager::GetLogger(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return GetLoggerInternal(name);
    }

    Logger* LogManager::GetLoggerInternal(const std::string& name)
    {
        auto it = m_Loggers.find(name);
        if (it != m_Loggers.end())
        {
            return it->second.get();
        }

        // Create new logger
        auto logger = std::make_unique<Logger>(name);
        auto loggerPtr = logger.get();
        
        // Ensure the logger was created successfully
        if (!loggerPtr || !loggerPtr->GetSpdLogger())
        {
            // If logger creation failed, try to create a fallback logger
            logger = std::make_unique<Logger>("fallback");
            loggerPtr = logger.get();
        }
        
        m_Loggers[name] = std::move(logger);
        
        // Apply global settings
        if (loggerPtr)
        {
            ApplyGlobalSettings(loggerPtr);
        }
        
        return loggerPtr;
    }

    void LogManager::SetGlobalLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        m_GlobalLevel = level;
        
        // Apply to all existing loggers
        for (auto& [name, logger] : m_Loggers)
        {
            logger->SetLevel(level);
        }
    }

    LogLevel LogManager::GetGlobalLevel() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_GlobalLevel;
    }

    void LogManager::SetGlobalPattern(const std::string& pattern)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        m_GlobalPattern = pattern;
        
        // Apply to all existing loggers
        for (auto& [name, logger] : m_Loggers)
        {
            logger->SetPattern(pattern);
        }
    }

    std::string LogManager::GetGlobalPattern() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_GlobalPattern;
    }

    void LogManager::EnableGlobalFileLogging(const std::string& logDir)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        m_LogDirectory = logDir;
        m_FileLoggingEnabled = true;
        
        // Apply to all existing loggers
        for (auto& [name, logger] : m_Loggers)
        {
            logger->EnableFileLogging(logDir + "/" + name + ".log", true, false); // Use daily logging by default
        }
    }

    void LogManager::DisableGlobalFileLogging()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        m_FileLoggingEnabled = false;
        
        // Apply to all existing loggers
        for (auto& [name, logger] : m_Loggers)
        {
            logger->DisableFileLogging();
        }
    }

    void LogManager::EnablePerformanceLogging(bool enable)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_PerformanceLoggingEnabled = enable;
        
        if (auto logger = GetDefaultLoggerInternal())
        {
            if (enable)
            {
                logger->Info("Performance logging enabled");
            }
            else
            {
                logger->Info("Performance logging disabled");
            }
        }
    }

    void LogManager::EnableMemoryLogging(bool enable)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MemoryLoggingEnabled = enable;
        
        if (auto logger = GetDefaultLoggerInternal())
        {
            if (enable)
            {
                logger->Info("Memory logging enabled");
            }
            else
            {
                logger->Info("Memory logging disabled");
            }
        }
    }

    void LogManager::LogSystemInfo()
    {
        // Use GetLogger instead of GetDefaultLogger to avoid potential recursive locking
        if (auto logger = GetLogger("default"))
        {
            logger->LogSystemInfo();
        }
    }

    void LogManager::FlushAll()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        FlushAllInternal();
    }

    void LogManager::ClearAllLoggers()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        // Flush all loggers before clearing
        FlushAllInternal();
        
        // Clear all loggers from our map
        m_Loggers.clear();
        
        // Also clear spdlog's internal logger registry to prevent "logger already exists" errors
        try
        {
            spdlog::drop_all();
        }
        catch (...)
        {
            // If spdlog::drop_all() fails, continue anyway
        }
    }

    void LogManager::FlushAllInternal()
    {
        for (auto& [name, logger] : m_Loggers)
        {
            logger->Flush();
        }
    }

    std::vector<LogManager::LoggerStats> LogManager::GetLoggerStats() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        std::vector<LoggerStats> stats;
        stats.reserve(m_Loggers.size());
        
        for (const auto& [name, logger] : m_Loggers)
        {
            LoggerStats stat;
            stat.name = name;
            stat.level = logger->GetLevel();
            // Note: spdlog doesn't provide message counts by default
            // This would need to be implemented with custom sinks if needed
            stat.messageCount = 0;
            stat.errorCount = 0;
            stats.push_back(stat);
        }
        
        return stats;
    }

    void LogManager::ApplyGlobalSettings(Logger* logger)
    {
        if (!logger) return;
        
        // Apply global level
        logger->SetLevel(m_GlobalLevel);
        
        // Apply global pattern
        if (!m_GlobalPattern.empty())
        {
            logger->SetPattern(m_GlobalPattern);
        }
        
        // Apply file logging if enabled
        if (m_FileLoggingEnabled && !m_LogDirectory.empty())
        {
            // We need to know the logger name to create a proper filename
            // For now, use a default name
            logger->EnableFileLogging(m_LogDirectory + "/default.log", true, false);
        }
    }
}