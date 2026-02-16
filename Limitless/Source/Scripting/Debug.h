#pragma once

#include <cstdint>
#include <string_view>

namespace Limitless
{
    enum class ScriptLogSeverity : int32_t
    {
        Info = 0,
        Warning = 1,
        Error = 2
    };

    using ScriptLogBridgeCallback = void (*)(ScriptLogSeverity severity, const char* message);

    class Debug final
    {
    public:
        Debug() = delete;

        static void SetLogBridgeCallback(ScriptLogBridgeCallback callback);

        static void Log(std::string_view message);
        static void LogWarning(std::string_view message);
        static void LogError(std::string_view message);
    };
}
