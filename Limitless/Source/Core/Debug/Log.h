#pragma once

#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bundled/format.h>
#include <variant>
#include <string>

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
#define LT_CORE_TRACE(...)                   \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define LT_CORE_DEBUG(...)                   \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->debug(__VA_ARGS__)
#define LT_CORE_INFO(...)                    \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->info(__VA_ARGS__)
#define LT_CORE_WARN(...)                    \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define LT_CORE_ERROR(...)                   \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->error(__VA_ARGS__)
#define LT_CORE_CRITICAL(...)                \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client logging macros (for application/sandbox use)
#define LT_TRACE(...)                        \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->trace(__VA_ARGS__)
#define LT_DEBUG(...)                        \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->debug(__VA_ARGS__)
#define LT_INFO(...)                         \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->info(__VA_ARGS__)
#define LT_WARN(...)                         \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->warn(__VA_ARGS__)
#define LT_ERROR(...)                        \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->error(__VA_ARGS__)
#define LT_CRITICAL(...)                     \
    if (!::Limitless::Log::IsShuttingDown()) \
    ::Limitless::Log::GetClientLogger()->critical(__VA_ARGS__)