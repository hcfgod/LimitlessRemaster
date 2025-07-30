#pragma once

#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bundled/format.h>
#include <variant>
#include <string>

// Build configuration-based logging settings
#ifdef LT_CONFIG_DEBUG
    #define LT_LOG_LEVEL_TRACE_ENABLED
    #define LT_LOG_LEVEL_DEBUG_ENABLED
    #define LT_LOG_LEVEL_INFO_ENABLED
    #define LT_LOG_LEVEL_WARN_ENABLED
    #define LT_LOG_LEVEL_ERROR_ENABLED
    #define LT_LOG_LEVEL_CRITICAL_ENABLED
#elif defined(LT_CONFIG_RELEASE)
    #define LT_LOG_LEVEL_INFO_ENABLED
    #define LT_LOG_LEVEL_WARN_ENABLED
    #define LT_LOG_LEVEL_ERROR_ENABLED
    #define LT_LOG_LEVEL_CRITICAL_ENABLED
#elif defined(LT_CONFIG_DIST)
    #define LT_LOG_LEVEL_WARN_ENABLED
    #define LT_LOG_LEVEL_ERROR_ENABLED
    #define LT_LOG_LEVEL_CRITICAL_ENABLED
#endif

// Console logging settings
#ifdef LT_LOG_CONSOLE_ENABLED
    #define LT_CONSOLE_LOGGING_ENABLED
#endif

// Core logging settings
#ifdef LT_LOG_CORE_ENABLED
    #define LT_CORE_LOGGING_ENABLED
#endif

// Forward declaration of ConfigValue
namespace Limitless {
    using ConfigValue = std::variant<bool, int, float, double, std::string, size_t, uint32_t>;
}

// fmt formatter for ConfigValue
template<>
struct fmt::formatter<Limitless::ConfigValue> : formatter<std::string> {
    template<typename FormatContext>
    auto format(const Limitless::ConfigValue& value, FormatContext& ctx) const {
        std::string result;
        std::visit([&result](const auto& v) {
            result = fmt::format("{}", v);
        }, value);
        return formatter<std::string>::format(result, ctx);
    }
};

namespace Limitless {
class Log {
public:
    // Initialize with default settings
    static void Init();
    
    // Initialize with configuration settings
    static void Init(const std::string& logLevel, 
                     bool fileEnabled, 
                     bool consoleEnabled, 
                     const std::string& pattern,
                     const std::string& directory,
                     size_t maxFileSize,
                     size_t maxFiles);
    
    // Initialize from configuration
    static void InitFromConfig();
    
    static void Shutdown();
    static bool IsShuttingDown() {
        return s_IsShuttingDown;
    }
    
    static bool IsInitialized() {
        return s_CoreLogger != nullptr || s_ClientLogger != nullptr;
    }

    static std::shared_ptr<spdlog::logger>& GetCoreLogger() {
        return s_CoreLogger;
    }
    static std::shared_ptr<spdlog::logger>& GetClientLogger() {
        return s_ClientLogger;
    }

private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
    static bool s_IsShuttingDown;
    
    // Helper method to convert string log level to spdlog level
    static spdlog::level::level_enum StringToLogLevel(const std::string& level);
};
} // namespace Limitless

// Core logging macros (for engine internal use)
#ifdef LT_CORE_LOGGING_ENABLED
    #ifdef LT_LOG_LEVEL_TRACE_ENABLED
        #ifndef LT_CORE_TRACE
        #define LT_CORE_TRACE(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->trace(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_TRACE(...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_DEBUG_ENABLED
        #ifndef LT_CORE_DEBUG
        #define LT_CORE_DEBUG(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->debug(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_DEBUG(...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_INFO_ENABLED
        #ifndef LT_CORE_INFO
        #define LT_CORE_INFO(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->info(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_INFO(...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_WARN_ENABLED
        #ifndef LT_CORE_WARN
        #define LT_CORE_WARN(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->warn(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_WARN(...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_ERROR_ENABLED
        #ifndef LT_CORE_ERROR
        #define LT_CORE_ERROR(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->error(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_ERROR(...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_CRITICAL_ENABLED
        #ifndef LT_CORE_CRITICAL
        #define LT_CORE_CRITICAL(...) \
            do { \
                if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->critical(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_CRITICAL(...) ((void)0)
    #endif
#else
    // Core logging disabled
    #define LT_CORE_TRACE(...) ((void)0)
    #define LT_CORE_DEBUG(...) ((void)0)
    #define LT_CORE_INFO(...) ((void)0)
    #define LT_CORE_WARN(...) ((void)0)
    #define LT_CORE_ERROR(...) ((void)0)
    #define LT_CORE_CRITICAL(...) ((void)0)
#endif

// Client logging macros (for application/sandbox use)
#ifdef LT_LOG_LEVEL_TRACE_ENABLED
    #ifndef LT_TRACE
    #define LT_TRACE(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->trace(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_TRACE(...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_DEBUG_ENABLED
    #ifndef LT_DBG
    #define LT_DBG(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->debug(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_DBG(...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_INFO_ENABLED
    #ifndef LT_INFO
    #define LT_INFO(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->info(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_INFO(...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_WARN_ENABLED
    #ifndef LT_WARN
    #define LT_WARN(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->warn(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_WARN(...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_ERROR_ENABLED
    #ifndef LT_ERROR
    #define LT_ERROR(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->error(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_ERROR(...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_CRITICAL_ENABLED
    #ifndef LT_CRITICAL
    #define LT_CRITICAL(...) \
        do { \
            if (::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->critical(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_CRITICAL(...) ((void)0)
#endif

// Conditional logging macros (for application/sandbox use)
#ifdef LT_LOG_LEVEL_TRACE_ENABLED
    #ifndef LT_TRACE_IF
    #define LT_TRACE_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->trace(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_TRACE_IF(condition, ...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_DEBUG_ENABLED
    #ifndef LT_DBG_IF
    #define LT_DBG_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->debug(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_DBG_IF(condition, ...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_INFO_ENABLED
    #ifndef LT_INFO_IF
    #define LT_INFO_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->info(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_INFO_IF(condition, ...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_WARN_ENABLED
    #ifndef LT_WARN_IF
    #define LT_WARN_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->warn(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_WARN_IF(condition, ...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_ERROR_ENABLED
    #ifndef LT_ERROR_IF
    #define LT_ERROR_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->error(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_ERROR_IF(condition, ...) ((void)0)
#endif

#ifdef LT_LOG_LEVEL_CRITICAL_ENABLED
    #ifndef LT_CRITICAL_IF
    #define LT_CRITICAL_IF(condition, ...) \
        do { \
            if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetClientLogger()) { \
                ::Limitless::Log::GetClientLogger()->critical(__VA_ARGS__); \
            } \
        } while(0)
    #endif
#else
    #define LT_CRITICAL_IF(condition, ...) ((void)0)
#endif

// Conditional core logging macros (for engine internal use)
#ifdef LT_CORE_LOGGING_ENABLED
    #ifdef LT_LOG_LEVEL_TRACE_ENABLED
        #ifndef LT_CORE_TRACE_IF
        #define LT_CORE_TRACE_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->trace(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_TRACE_IF(condition, ...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_DEBUG_ENABLED
        #ifndef LT_CORE_DEBUG_IF
        #define LT_CORE_DEBUG_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->debug(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_DEBUG_IF(condition, ...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_INFO_ENABLED
        #ifndef LT_CORE_INFO_IF
        #define LT_CORE_INFO_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->info(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_INFO_IF(condition, ...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_WARN_ENABLED
        #ifndef LT_CORE_WARN_IF
        #define LT_CORE_WARN_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->warn(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_WARN_IF(condition, ...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_ERROR_ENABLED
        #ifndef LT_CORE_ERROR_IF
        #define LT_CORE_ERROR_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->error(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_ERROR_IF(condition, ...) ((void)0)
    #endif

    #ifdef LT_LOG_LEVEL_CRITICAL_ENABLED
        #ifndef LT_CORE_CRITICAL_IF
        #define LT_CORE_CRITICAL_IF(condition, ...) \
            do { \
                if ((condition) && ::Limitless::Log::IsInitialized() && !::Limitless::Log::IsShuttingDown() && ::Limitless::Log::GetCoreLogger()) { \
                    ::Limitless::Log::GetCoreLogger()->critical(__VA_ARGS__); \
                } \
            } while(0)
        #endif
    #else
        #define LT_CORE_CRITICAL_IF(condition, ...) ((void)0)
    #endif
#else
    // Core logging disabled
    #define LT_CORE_TRACE_IF(condition, ...) ((void)0)
    #define LT_CORE_DEBUG_IF(condition, ...) ((void)0)
    #define LT_CORE_INFO_IF(condition, ...) ((void)0)
    #define LT_CORE_WARN_IF(condition, ...) ((void)0)
    #define LT_CORE_ERROR_IF(condition, ...) ((void)0)
    #define LT_CORE_CRITICAL_IF(condition, ...) ((void)0)
#endif