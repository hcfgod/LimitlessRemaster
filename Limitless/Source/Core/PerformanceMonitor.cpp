#include "PerformanceMonitor.h"
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace Limitless
{
    PerformanceMonitor::Scope::Scope(const std::string& name, Logger* logger)
        : m_Name(name), m_Logger(logger), m_StartTime(Clock::now()), m_Ended(false)
    {
    }

    PerformanceMonitor::Scope::~Scope()
    {
        if (!m_Ended)
        {
            End();
        }
    }

    double PerformanceMonitor::Scope::GetElapsedMs() const
    {
        auto now = Clock::now();
        return DurationMs(m_StartTime, now);
    }

    PerformanceMonitor::Duration PerformanceMonitor::Scope::GetElapsedDuration() const
    {
        auto now = Clock::now();
        return std::chrono::duration_cast<Duration>(now - m_StartTime);
    }

    void PerformanceMonitor::Scope::End()
    {
        if (m_Ended) return;
        
        auto endTime = Clock::now();
        double ms = DurationMs(m_StartTime, endTime);
        
        // Add comprehensive safety checks to prevent logging if logger is invalid
        if (m_Logger && m_Logger->GetSpdLogger())
        {
            try
            {
                // Additional check to ensure the logger is still valid
                if (m_Logger->GetSpdLogger().get() != nullptr)
                {
                    m_Logger->LogPerformance(m_Name, ms);
                }
            }
            catch (...)
            {
                // If logging fails (e.g., logger was destroyed), just continue silently
                // This prevents access violations during shutdown
            }
        }
        
        m_Ended = true;
    }

    double PerformanceMonitor::TimeFunction(const std::string& name, std::function<void()> func, Logger* logger)
    {
        auto start = Clock::now();
        func();
        auto end = Clock::now();
        
        double ms = DurationMs(start, end);
        
        // Add comprehensive safety checks to prevent logging if logger is invalid
        if (logger && logger->GetSpdLogger())
        {
            try
            {
                // Additional check to ensure the logger is still valid
                if (logger->GetSpdLogger().get() != nullptr)
                {
                    logger->LogPerformance(name, ms);
                }
            }
            catch (...)
            {
                // If logging fails (e.g., logger was destroyed), just continue silently
                // This prevents access violations during shutdown
            }
        }
        
        return ms;
    }

    std::unique_ptr<PerformanceMonitor::Scope> PerformanceMonitor::CreateScope(const std::string& name, Logger* logger)
    {
        return std::make_unique<Scope>(name, logger);
    }

    size_t PerformanceMonitor::GetCurrentMemoryUsage()
    {
        #ifdef _WIN32
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
            {
                return pmc.WorkingSetSize;
            }
        #elif defined(__APPLE__)
            struct task_basic_info t_info;
            mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
            
            if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count) == KERN_SUCCESS)
            {
                return t_info.resident_size;
            }
        #else
            FILE* file = fopen("/proc/self/status", "r");
            if (file)
            {
                char line[128];
                while (fgets(line, 128, file) != NULL)
                {
                    if (strncmp(line, "VmRSS:", 6) == 0)
                    {
                        long rss;
                        if (sscanf(line, "VmRSS: %ld", &rss) == 1)
                        {
                            fclose(file);
                            return rss * 1024; // Convert KB to bytes
                        }
                    }
                }
                fclose(file);
            }
        #endif
        
        return 0; // Unable to get memory usage
    }

    void PerformanceMonitor::LogMemoryUsage(const std::string& context, Logger* logger)
    {
        // Add comprehensive safety checks to prevent logging if logger is invalid
        if (!logger || !logger->GetSpdLogger()) return;
        
        try
        {
            // Additional check to ensure the logger is still valid
            if (logger->GetSpdLogger().get() != nullptr)
            {
                size_t memoryUsage = GetCurrentMemoryUsage();
                logger->LogMemoryUsage(memoryUsage, context);
            }
        }
        catch (...)
        {
            // If logging fails (e.g., logger was destroyed), just continue silently
            // This prevents access violations during shutdown
        }
    }
}