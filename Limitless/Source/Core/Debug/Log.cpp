#include "Log.h"
#include "Core/ConfigManager.h"
#include <filesystem>
#include <iostream>
#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/bundled/format.h>

#if defined(LIMITLESS_PLATFORM_WINDOWS)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace Limitless 
{
    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
    bool Log::s_IsShuttingDown = false;

    spdlog::level::level_enum Log::StringToLogLevel(const std::string& level) {
        if (level == "trace") return spdlog::level::trace;
        if (level == "debug") return spdlog::level::debug;
        if (level == "info") return spdlog::level::info;
        if (level == "warn") return spdlog::level::warn;
        if (level == "error") return spdlog::level::err;
        if (level == "critical") return spdlog::level::critical;
        if (level == "off") return spdlog::level::off;
    
        // Default to info if unknown
        return spdlog::level::info;
    }

    void Log::Init() {
        // Use default configuration
        Init("info", true, true, "[%T] [%l] %n: %v", "logs", 1048576 * 50, 10);
    }

    void Log::InitFromConfig() {
        // Get configuration values
        auto& config = GetConfigManager();
        
        std::string logLevel = config.GetValue<std::string>(Config::Logging::LEVEL, "info");
        bool fileEnabled = config.GetValue<bool>(Config::Logging::FILE_ENABLED, true);
        bool consoleEnabled = config.GetValue<bool>(Config::Logging::CONSOLE_ENABLED, true);
        std::string pattern = config.GetValue<std::string>(Config::Logging::PATTERN, "[%T] [%l] %n: %v");
        std::string directory = config.GetValue<std::string>(Config::Logging::DIRECTORY, "logs");
        size_t maxFileSize = config.GetValue<size_t>(Config::Logging::MAX_FILE_SIZE, 1048576 * 50);
        size_t maxFiles = config.GetValue<size_t>(Config::Logging::MAX_FILES, 10);
        
        // Log the configuration values being used (before logging is initialized)
        std::cout << "Logging configuration:" << std::endl;
        std::cout << "  Level: " << logLevel << std::endl;
        std::cout << "  File enabled: " << (fileEnabled ? "true" : "false") << std::endl;
        std::cout << "  Console enabled: " << (consoleEnabled ? "true" : "false") << std::endl;
        std::cout << "  Pattern: " << pattern << std::endl;
        std::cout << "  Directory: " << directory << std::endl;
        std::cout << "  Max file size: " << (maxFileSize / (1024 * 1024)) << "MB" << std::endl;
        std::cout << "  Max files: " << maxFiles << std::endl;
        
        Init(logLevel, fileEnabled, consoleEnabled, pattern, directory, maxFileSize, maxFiles);
    }

    void Log::Init(const std::string& logLevel, 
                   bool fileEnabled, 
                   bool consoleEnabled, 
                   const std::string& pattern,
                   const std::string& directory,
                   size_t maxFileSize,
                   size_t maxFiles) {
        // If already initialized, shutdown first
        if (IsInitialized()) {
            Shutdown();
        }
        
        // Reset shutdown flag
        s_IsShuttingDown = false;

        // Ensure logs directory exists
        std::filesystem::create_directory(directory);

        // ---------------------------------------------------------------------------------
        // Windows-specific: Ensure UTF-8 code page so that Unicode characters in logs are
        // rendered correctly in the console.  We only do this once at startup.
        // ---------------------------------------------------------------------------------
    #if defined(LT_PLATFORM_WINDOWS)
        UINT currentCP = GetConsoleOutputCP();
        if (currentCP != CP_UTF8) {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
            // Switch CRT file descriptors (stdout/stderr) to UTF-8 text mode so that
            // wide characters emitted by spdlog make it through.
            _setmode(_fileno(stdout), _O_U8TEXT);
            _setmode(_fileno(stderr), _O_U8TEXT);
        }
    #endif

        // ---------------------------------------------------------------------------------
        // Set up an asynchronous logging thread pool (single background thread is enough)
        // ---------------------------------------------------------------------------------
        constexpr size_t kQueueSize = 8192; // max pending log messages
        constexpr size_t kThreadCount = 1;  // background logging threads

        spdlog::init_thread_pool(kQueueSize, kThreadCount);
        auto threadPool = spdlog::thread_pool();

        // Convert log level
        auto level = StringToLogLevel(logLevel);

        // Sinks ---------------------------------------------------
        std::vector<spdlog::sink_ptr> coreSinks;
        std::vector<spdlog::sink_ptr> clientSinks;

        // Console sink
        if (consoleEnabled) {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_pattern("%^[%T] %n: %v%$");
            clientSinks.push_back(consoleSink);
        
            // Core logger gets console only in debug builds
    #if defined(LT_BUILD_DEBUG)
            coreSinks.push_back(consoleSink);
    #endif
        }

        // File sink
        if (fileEnabled) {
            std::string logFilePath = directory + "/Limitless.log";
            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, maxFileSize, maxFiles);
            fileSink->set_pattern(pattern);
            coreSinks.push_back(fileSink);
            clientSinks.push_back(fileSink);
        }

        // Create Core logger (engine internals)
        if (!coreSinks.empty()) {
            s_CoreLogger = std::make_shared<spdlog::async_logger>("LIMITLESS", coreSinks.begin(), coreSinks.end(), threadPool,
                                                                  spdlog::async_overflow_policy::block);
            spdlog::register_logger(s_CoreLogger);
            s_CoreLogger->set_level(level);
            s_CoreLogger->flush_on(level);
        }

        // Create Client logger (application layer) - asynchronous
        if (!clientSinks.empty()) {
            s_ClientLogger = std::make_shared<spdlog::async_logger>("APP", clientSinks.begin(), clientSinks.end(), threadPool,
                                                                    spdlog::async_overflow_policy::block);
            spdlog::register_logger(s_ClientLogger);
            s_ClientLogger->set_level(level);
            s_ClientLogger->flush_on(level);
        }

        // Use std::cout for initialization messages since logging isn't ready yet
        std::cout << "Limitless Engine Logger Initialized!" << std::endl;
        if (fileEnabled) {
            std::cout << "Log rotation: " << (maxFileSize / (1024 * 1024)) << "MB max file size, " << maxFiles << " backup files" << std::endl;
        }
        std::cout << "Application Logger Initialized!" << std::endl;
    }

    void Log::Shutdown() 
    {
        // Set shutdown flag FIRST to prevent any further logging calls
        s_IsShuttingDown = true;

        // Call spdlog::shutdown() BEFORE resetting logger pointers
        // This ensures all loggers are properly cleaned up before we release our references
        spdlog::shutdown();

        // Now safely reset our logger pointers
        s_CoreLogger.reset();
        s_ClientLogger.reset();
    }
} // namespace Limitless