#pragma once

#include "Logger.h"
#include <chrono>
#include <string>
#include <memory>
#include <functional>
#include <type_traits>

namespace Limitless
{
    class PerformanceMonitor
    {
    public:
        using Clock = std::chrono::high_resolution_clock;
        using TimePoint = Clock::time_point;
        using Duration = std::chrono::microseconds;

        class Scope
        {
        public:
            Scope(const std::string& name, Logger* logger = nullptr);
            ~Scope();

            // Disable copy
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

            // Allow move
            Scope(Scope&&) = default;
            Scope& operator=(Scope&&) = default;

            // Get elapsed time
            double GetElapsedMs() const;
            Duration GetElapsedDuration() const;

            // End timing early
            void End();

        private:
            std::string m_Name;
            Logger* m_Logger;
            TimePoint m_StartTime;
            bool m_Ended;
        };

        // Static timing methods
        static double TimeFunction(const std::string& name, std::function<void()> func, 
                                 Logger* logger = nullptr);
        
        // Template for non-void return types
        template<typename Func, typename... Args>
        static auto TimeFunction(const std::string& name, Func&& func, Args&&... args, 
                               Logger* logger = nullptr) 
            -> std::enable_if_t<!std::is_void_v<std::invoke_result_t<Func, Args...>>, 
                               std::invoke_result_t<Func, Args...>>
        {
            auto start = Clock::now();
            auto result = func(std::forward<Args>(args)...);
            auto end = Clock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start);
            double ms = duration.count() / 1000.0;
            
            // Add safety check to prevent logging if logger is invalid
            if (logger && logger->GetSpdLogger())
            {
                try
                {
                    logger->LogPerformance(name, ms);
                }
                catch (...)
                {
                    // If logging fails (e.g., logger was destroyed), just continue silently
                }
            }
            
            return result;
        }

        // Template specialization for void return types
        template<typename Func, typename... Args>
        static auto TimeFunction(const std::string& name, Func&& func, Args&&... args, 
                               Logger* logger = nullptr) 
            -> std::enable_if_t<std::is_void_v<std::invoke_result_t<Func, Args...>>, void>
        {
            auto start = Clock::now();
            func(std::forward<Args>(args)...);
            auto end = Clock::now();
            
            auto duration = std::chrono::duration_cast<Duration>(end - start);
            double ms = duration.count() / 1000.0;
            
            // Add safety check to prevent logging if logger is invalid
            if (logger && logger->GetSpdLogger())
            {
                try
                {
                    logger->LogPerformance(name, ms);
                }
                catch (...)
                {
                    // If logging fails (e.g., logger was destroyed), just continue silently
                }
            }
        }

        // Create a scoped timer
        static std::unique_ptr<Scope> CreateScope(const std::string& name, Logger* logger = nullptr);

        // Get current timestamp
        static TimePoint Now() { return Clock::now(); }

        // Calculate duration between two time points
        static double DurationMs(const TimePoint& start, const TimePoint& end)
        {
            auto duration = std::chrono::duration_cast<Duration>(end - start);
            return duration.count() / 1000.0;
        }

        // Memory usage tracking
        static size_t GetCurrentMemoryUsage();
        static void LogMemoryUsage(const std::string& context = "", Logger* logger = nullptr);

    private:
        PerformanceMonitor() = default;
    };

    // Convenience macros for performance monitoring
    #define LT_PERF_SCOPE(name) \
        auto LT_CONCAT(perf_scope_, __LINE__) = Limitless::PerformanceMonitor::CreateScope(name, \
            Limitless::LogManager::GetInstance().IsInitialized() ? Limitless::GetDefaultLogger() : nullptr)

    #define LT_PERF_SCOPE_LOGGER(name, logger) \
        auto LT_CONCAT(perf_scope_, __LINE__) = Limitless::PerformanceMonitor::CreateScope(name, logger)

    #define LT_PERF_FUNCTION(name, func) \
        Limitless::PerformanceMonitor::TimeFunction(name, func, \
            Limitless::LogManager::GetInstance().IsInitialized() ? Limitless::GetDefaultLogger() : nullptr)

    #define LT_PERF_FUNCTION_LOGGER(name, func, logger) \
        Limitless::PerformanceMonitor::TimeFunction(name, func, logger)

    // Helper macro for concatenation
    #define LT_CONCAT(a, b) LT_CONCAT_INNER(a, b)
    #define LT_CONCAT_INNER(a, b) a##b

    // Memory tracking macros
    #define LT_LOG_MEMORY(context) \
        Limitless::PerformanceMonitor::LogMemoryUsage(context, \
            Limitless::LogManager::GetInstance().IsInitialized() ? Limitless::GetDefaultLogger() : nullptr)

    #define LT_LOG_MEMORY_LOGGER(context, logger) \
        Limitless::PerformanceMonitor::LogMemoryUsage(context, logger)
}