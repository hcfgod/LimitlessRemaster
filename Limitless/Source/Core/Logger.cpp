#include "Logger.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <mach/mach_host.h>
#else
    #include <sys/sysinfo.h>
#endif

namespace Limitless
{
    Logger::Logger(const std::string& name)
    {
        try
        {
            // First check if a logger with this name already exists
            auto existingLogger = spdlog::get(name);
            if (existingLogger)
            {
                // Use the existing logger
                m_Logger = existingLogger;
            }
            else
            {
                // Try to create a new logger using spdlog's factory method
                m_Logger = spdlog::create<spdlog::sinks::stdout_color_sink_mt>(name);
            }
            
            // If creation failed, use the default logger
            if (!m_Logger)
            {
                m_Logger = spdlog::default_logger();
            }
            
            // If still no logger, create a basic one manually
            if (!m_Logger)
            {
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                m_Logger = std::make_shared<spdlog::logger>(name, console_sink);
                spdlog::register_logger(m_Logger);
            }
            
            // Configure the logger
            if (m_Logger)
            {
                m_Logger->set_level(spdlog::level::trace);
                m_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
            }
            else
            {
                // If we still don't have a logger, this is a critical failure
                // We'll create a minimal fallback that just writes to stdout
                // This should never happen, but better safe than sorry
            }
        }
        catch (const std::exception& e)
        {
            // Log the exception to stderr since we can't use the logger
            std::cerr << "Logger creation failed: " << e.what() << std::endl;
            
            // Fallback to default logger if anything goes wrong
            m_Logger = spdlog::default_logger();
            if (m_Logger)
            {
                m_Logger->set_level(spdlog::level::trace);
                m_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
            }
            else
            {
                // Last resort - create a minimal logger
                try
                {
                    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    m_Logger = std::make_shared<spdlog::logger>(name, console_sink);
                }
                catch (...)
                {
                    // If even this fails, we'll have a null logger
                    // The logging methods will handle this gracefully
                }
            }
        }
        catch (...)
        {
            // Fallback to default logger if anything goes wrong
            m_Logger = spdlog::default_logger();
            if (m_Logger)
            {
                m_Logger->set_level(spdlog::level::trace);
                m_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
            }
            else
            {
                // Last resort - create a minimal logger
                try
                {
                    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    m_Logger = std::make_shared<spdlog::logger>(name, console_sink);
                }
                catch (...)
                {
                    // If even this fails, we'll have a null logger
                    // The logging methods will handle this gracefully
                }
            }
        }
    }

    void Logger::Trace(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->trace(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::Debug(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->debug(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::Info(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->info(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::Warn(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->warn(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::Error(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->error(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::Critical(const std::string& message)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->critical(message);
            }
            catch (...)
            {
                // If logging fails, we can't log the error, so just continue
            }
        }
    }

    void Logger::SetupDefaultSinks()
    {
        // Console sink with colors - this is now handled in the constructor
        // when we create the logger with spdlog::create<spdlog::sinks::stdout_color_sink_mt>
        // The m_Sinks vector is kept for additional sinks added later
    }

    void Logger::SetupFileSinks(const std::string& filename, bool daily, bool rotating)
    {
        // Create log directory if it doesn't exist
        std::filesystem::path logDir = std::filesystem::path(filename).parent_path();
        if (!logDir.empty())
        {
            std::filesystem::create_directories(logDir);
        }

        // Create the appropriate file sink
        std::shared_ptr<spdlog::sinks::sink> fileSink;
        
        if (daily)
        {
            // Daily rotating file sink
            fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(filename, 0, 0);
            fileSink->set_level(spdlog::level::debug);
        }
        else if (rotating)
        {
            // Rotating file sink for detailed logs
            fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename, 1024 * 1024 * 5, 3); // 5MB files, keep 3 files
            fileSink->set_level(spdlog::level::trace);
        }
        else
        {
            // Basic file sink - use rotating sink with large size for basic logging
            fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename, 1024 * 1024 * 10, 1); // 10MB file, keep 1 file
            fileSink->set_level(spdlog::level::debug);
        }

        // Store the sink for later reference
        m_Sinks.push_back(fileSink);
        
        // Create a new logger with both console and file sinks
        std::vector<std::shared_ptr<spdlog::sinks::sink>> allSinks;
        
        // Add console sink (stdout_color_sink_mt)
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::trace);
        allSinks.push_back(consoleSink);
        
        // Add file sink
        allSinks.push_back(fileSink);
        
        // Create new logger with multiple sinks
        m_Logger = std::make_shared<spdlog::logger>(m_Logger->name(), allSinks.begin(), allSinks.end());
        
        // Re-apply settings
        m_Logger->set_level(spdlog::level::trace);
        m_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    }

    void Logger::LogSystemInfo()
    {
        Info("=== System Information ===");
        
        // Platform info
        #ifdef _WIN32
            Info("Platform: Windows");
            #ifdef _WIN64
                Info("Architecture: x64");
            #else
                Info("Architecture: x86");
            #endif
        #elif defined(__APPLE__)
            Info("Platform: macOS");
            #ifdef __x86_64__
                Info("Architecture: x64");
            #elif defined(__arm64__)
                Info("Architecture: ARM64");
            #endif
        #else
            Info("Platform: Linux");
            #ifdef __x86_64__
                Info("Architecture: x64");
            #elif defined(__aarch64__)
                Info("Architecture: ARM64");
            #endif
        #endif

        // Compiler info
        #ifdef _MSC_VER
            Info(spdlog::fmt_lib::format("Compiler: MSVC {}", _MSC_VER));
        #elif defined(__clang__)
            Info(spdlog::fmt_lib::format("Compiler: Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__));
        #elif defined(__GNUC__)
            Info(spdlog::fmt_lib::format("Compiler: GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__));
        #endif

        // Memory info
        LogMemoryUsage(0, "System Info");
    }

    void Logger::LogPerformance(const std::string& operation, double durationMs)
    {
        if (durationMs < 1.0)
        {
            Debug(spdlog::fmt_lib::format("Performance: {} took {:.3f} ms", operation, durationMs));
        }
        else if (durationMs < 100.0)
        {
            Info(spdlog::fmt_lib::format("Performance: {} took {:.2f} ms", operation, durationMs));
        }
        else
        {
            Warn(spdlog::fmt_lib::format("Performance: {} took {:.1f} ms (slow)", operation, durationMs));
        }
    }

    void Logger::LogMemoryUsage(size_t bytes, const std::string& context)
    {
        if (bytes == 0)
        {
            // Log system memory info
            #ifdef _WIN32
                MEMORYSTATUSEX memInfo;
                memInfo.dwLength = sizeof(MEMORYSTATUSEX);
                if (GlobalMemoryStatusEx(&memInfo))
                {
                    size_t totalPhys = memInfo.ullTotalPhys;
                    size_t availPhys = memInfo.ullAvailPhys;
                    size_t usedPhys = totalPhys - availPhys;
                    
                    Info(spdlog::fmt_lib::format("Memory: Total: {:.1f} GB, Used: {:.1f} GB, Available: {:.1f} GB", 
                         totalPhys / (1024.0 * 1024.0 * 1024.0),
                         usedPhys / (1024.0 * 1024.0 * 1024.0),
                         availPhys / (1024.0 * 1024.0 * 1024.0)));
                }
            #elif defined(__APPLE__)
                vm_size_t pageSize;
                host_page_size(mach_host_self(), &pageSize);
                
                vm_statistics64_data_t vmStats;
                mach_msg_type_number_t infoCount = sizeof(vmStats) / sizeof(natural_t);
                if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vmStats, &infoCount) == KERN_SUCCESS)
                {
                    size_t totalPhys = (vmStats.active_count + vmStats.inactive_count + vmStats.wire_count + vmStats.free_count) * pageSize;
                    size_t usedPhys = (vmStats.active_count + vmStats.wire_count) * pageSize;
                    size_t availPhys = (vmStats.inactive_count + vmStats.free_count) * pageSize;
                    
                    Info(spdlog::fmt_lib::format("Memory: Total: {:.1f} GB, Used: {:.1f} GB, Available: {:.1f} GB", 
                         totalPhys / (1024.0 * 1024.0 * 1024.0),
                         usedPhys / (1024.0 * 1024.0 * 1024.0),
                         availPhys / (1024.0 * 1024.0 * 1024.0)));
                }
            #else
                struct sysinfo si;
                if (sysinfo(&si) == 0)
                {
                    size_t totalPhys = si.totalram * si.mem_unit;
                    size_t freePhys = si.freeram * si.mem_unit;
                    size_t usedPhys = totalPhys - freePhys;
                    
                    Info(spdlog::fmt_lib::format("Memory: Total: {:.1f} GB, Used: {:.1f} GB, Available: {:.1f} GB", 
                         totalPhys / (1024.0 * 1024.0 * 1024.0),
                         usedPhys / (1024.0 * 1024.0 * 1024.0),
                         freePhys / (1024.0 * 1024.0 * 1024.0)));
                }
            #endif
        }
        else
        {
            // Log specific memory usage
            if (bytes < 1024)
            {
                Debug(spdlog::fmt_lib::format("Memory Usage{}: {} bytes", context.empty() ? "" : " (" + context + ")", bytes));
            }
            else if (bytes < 1024 * 1024)
            {
                Info(spdlog::fmt_lib::format("Memory Usage{}: {:.2f} KB", context.empty() ? "" : " (" + context + ")", bytes / 1024.0));
            }
            else
            {
                Info(spdlog::fmt_lib::format("Memory Usage{}: {:.2f} MB", context.empty() ? "" : " (" + context + ")", bytes / (1024.0 * 1024.0)));
            }
        }
    }

    void Logger::SetLevel(LogLevel level)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->set_level(static_cast<spdlog::level::level_enum>(level));
            }
            catch (...)
            {
                // If setting level fails, continue silently
            }
        }
    }

    LogLevel Logger::GetLevel() const
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                return static_cast<LogLevel>(m_Logger->level());
            }
            catch (...)
            {
                // If getting level fails, return default
            }
        }
        return LogLevel::Info; // Default fallback
    }

    void Logger::SetPattern(const std::string& pattern)
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->set_pattern(pattern);
            }
            catch (...)
            {
                // If setting pattern fails, continue silently
            }
        }
    }

    void Logger::EnableFileLogging(const std::string& filename, bool daily, bool rotating)
    {
        if (daily)
        {
            SetupFileSinks(filename, true, false);
        }
        else if (rotating)
        {
            SetupFileSinks(filename, false, true);
        }
        else
        {
            SetupFileSinks(filename, false, false);
        }
    }

    void Logger::DisableFileLogging()
    {
        // Clear file sinks
        m_Sinks.clear();
        
        // Recreate logger with only console sink
        m_Logger = spdlog::create<spdlog::sinks::stdout_color_sink_mt>(m_Logger->name());
        
        // Re-apply settings
        m_Logger->set_level(spdlog::level::trace);
        m_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    }

    void Logger::Flush()
    {
        if (m_Logger && m_Logger.get())
        {
            try
            {
                m_Logger->flush();
            }
            catch (...)
            {
                // If flushing fails, continue silently
            }
        }
    }
}