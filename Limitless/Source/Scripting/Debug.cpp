#include "Scripting/Debug.h"

#include "Core/Debug/Log.h"

#include <string>

namespace Limitless
{
    namespace
    {
        ScriptLogBridgeCallback s_ScriptLogBridgeCallback = nullptr;

        void LogInternal(ScriptLogSeverity severity, std::string_view message)
        {
#ifdef SCRIPTCORE_EXPORTS
            if (s_ScriptLogBridgeCallback)
            {
                const std::string messageText(message);
                s_ScriptLogBridgeCallback(severity, messageText.c_str());
            }
            return;
#else
            switch (severity)
            {
                case ScriptLogSeverity::Info:
                    LT_INFO("{}", message);
                    break;
                case ScriptLogSeverity::Warning:
                    LT_WARN("{}", message);
                    break;
                case ScriptLogSeverity::Error:
                    LT_ERROR("{}", message);
                    break;
            }
#endif
        }
    }

    void Debug::SetLogBridgeCallback(ScriptLogBridgeCallback callback)
    {
        s_ScriptLogBridgeCallback = callback;
    }

    void Debug::Log(std::string_view message)
    {
        LogInternal(ScriptLogSeverity::Info, message);
    }

    void Debug::LogWarning(std::string_view message)
    {
        LogInternal(ScriptLogSeverity::Warning, message);
    }

    void Debug::LogError(std::string_view message)
    {
        LogInternal(ScriptLogSeverity::Error, message);
    }
}
