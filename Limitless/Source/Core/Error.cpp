#include "Error.h"
#include "Core/Debug/Log.h"
#include "Platform/Platform.h"
#include <spdlog/fmt/fmt.h>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_map>

// Platform-specific includes for system error codes
#ifdef LT_PLATFORM_WINDOWS
    #include <windows.h>
    #include <errno.h>
#elif defined(LT_PLATFORM_MACOS) || defined(LT_PLATFORM_LINUX)
    #include <errno.h>
    #include <string.h>
#endif

namespace Limitless
{
    Error::Error(ErrorCode code, const std::string& message, const std::source_location& location, ErrorSeverity severity)
        : m_Code(code), m_Message(message), m_Severity(severity), m_SystemErrorCode(0)
    {
        // Helper function to clean up function names
        auto CleanFunctionName = [](const std::string& funcName) -> std::string {
            std::string cleaned = funcName;
            
            // Remove __cdecl calling convention
            size_t pos = cleaned.find("__cdecl ");
            if (pos != std::string::npos) {
                cleaned.erase(pos, 8); // Remove "__cdecl "
            }
            
            // Remove __stdcall calling convention
            pos = cleaned.find("__stdcall ");
            if (pos != std::string::npos) {
                cleaned.erase(pos, 10); // Remove "__stdcall "
            }
            
            // Remove __fastcall calling convention
            pos = cleaned.find("__fastcall ");
            if (pos != std::string::npos) {
                cleaned.erase(pos, 11); // Remove "__fastcall "
            }
            
            return cleaned;
        };
        
        // Format location string
        std::ostringstream oss;
        oss << location.file_name() << ":" << location.line() << ":" << location.column() 
            << " in " << CleanFunctionName(location.function_name());
        m_Location = oss.str();
        

        
        // Build context information
        BuildContext();
    }

    const char* Error::what() const noexcept
    {
        if (m_WhatBuffer.empty())
        {
            m_WhatBuffer = ToString();
        }
        return m_WhatBuffer.c_str();
    }

    std::string Error::ToString() const
    {
        std::ostringstream oss;
        oss << "[" << GetSeverityString() << "] " << GetErrorCodeString() << ": " << m_Message;
        if (!m_Location.empty())
        {
            oss << " at " << m_Location;
        }
        return oss.str();
    }

    std::string Error::ToDetailedString() const
    {
        std::ostringstream oss;
        oss << "=== Error Details ===" << std::endl;
        oss << "Severity: " << GetSeverityString() << std::endl;
        oss << "Code: " << GetErrorCodeString() << " (" << static_cast<int>(m_Code) << ")" << std::endl;
        oss << "Message: " << m_Message << std::endl;
        oss << "Location: " << m_Location << std::endl;
        
        if (m_SystemErrorCode != 0)
        {
            oss << "System Error: " << m_SystemErrorCode << " (" << ErrorHandling::GetSystemErrorString(m_SystemErrorCode) << ")" << std::endl;
        }
        
        if (!m_Context.functionName.empty())
        {
            oss << "Function: " << m_Context.functionName << std::endl;
        }
        
        if (!m_Context.className.empty())
        {
            oss << "Class: " << m_Context.className << std::endl;
        }
        
        if (!m_Context.moduleName.empty())
        {
            oss << "Module: " << m_Context.moduleName << std::endl;
        }
        
        if (!m_Context.threadId.empty())
        {
            oss << "Thread: " << m_Context.threadId << std::endl;
        }
        
        if (!m_Context.platformInfo.empty())
        {
            oss << "Platform: " << m_Context.platformInfo << std::endl;
        }
        
        if (!m_Context.systemInfo.empty())
        {
            oss << "System: " << m_Context.systemInfo << std::endl;
        }
        
        if (!m_Context.additionalData.empty())
        {
            oss << "Additional Data:" << std::endl;
            for (const auto& [key, value] : m_Context.additionalData)
            {
                oss << "  " << key << ": " << value << std::endl;
            }
        }
        
        oss << "Timestamp: " << m_Context.timestamp << std::endl;
        oss << "===================";
        
        return oss.str();
    }

    void Error::LogError(const Error& error)
    {
        // Try to use the logging system first
        if (Limitless::Log::IsInitialized() && !Limitless::Log::IsShuttingDown())
        {
            switch (error.GetSeverity())
            {
                case ErrorSeverity::Info:
                    LT_INFO("ERROR INFO: {}", error.ToString());
                    break;
                case ErrorSeverity::Warning:
                    LT_WARN("ERROR WARNING: {}", error.ToString());
                    break;
                case ErrorSeverity::Error:
                    LT_ERROR("ERROR: {}", error.ToString());
                    break;
                case ErrorSeverity::Critical:
                    LT_CRITICAL("CRITICAL ERROR: {}", error.ToString());
                    break;
                case ErrorSeverity::Fatal:
                    LT_CRITICAL("FATAL ERROR: {}", error.ToString());
                    break;
            }
            
            // Log detailed information for critical and fatal errors
            if (error.IsCritical())
            {
                LT_CRITICAL("Detailed Error Information:\n{}", error.ToDetailedString());
            }
        }
        else
        {
            // Fallback to cerr if logger is not available
            std::cerr << "ERROR: " << error.ToString() << std::endl;
            if (error.IsCritical())
            {
                std::cerr << "Detailed Error Information:\n" << error.ToDetailedString() << std::endl;
            }
        }
    }

    void Error::AddContext(const std::string& key, const std::string& value)
    {
        m_Context.additionalData[key] = value;
    }

    void Error::SetFunctionName(const std::string& functionName)
    {
        m_Context.functionName = functionName;
    }

    void Error::SetClassName(const std::string& className)
    {
        m_Context.className = className;
    }

    void Error::SetModuleName(const std::string& moduleName)
    {
        m_Context.moduleName = moduleName;
    }

    void Error::SetPlatformInfo(const PlatformInfo& platformInfo)
    {
        std::ostringstream oss;
        oss << platformInfo.platformName << " " << platformInfo.osVersion 
            << " (" << platformInfo.architectureName << ")";
        m_Context.platformInfo = oss.str();
    }

    void Error::SetSystemErrorCode(int systemErrorCode)
    {
        m_SystemErrorCode = systemErrorCode;
    }
    
    std::string Error::GetContextValue(const std::string& key) const
    {
        auto it = m_Context.additionalData.find(key);
        return (it != m_Context.additionalData.end()) ? it->second : "";
    }

    void Error::BuildContext()
    {
        // Set timestamp
        auto now = std::chrono::system_clock::now();
        m_Context.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        
        // Set thread ID
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        m_Context.threadId = oss.str();
        
        // Set platform info if available
        if (PlatformDetection::IsInitialized())
        {
            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            SetPlatformInfo(platformInfo);
        }
        
        // Set system info
        std::ostringstream sysInfo;
        sysInfo << "CPU Cores: " << PlatformDetection::GetCPUCount()
                << ", Memory: " << (PlatformDetection::GetTotalMemory() / (1024 * 1024)) << " MB";
        m_Context.systemInfo = sysInfo.str();
    }

    std::string Error::GetSeverityString() const
    {
        switch (m_Severity)
        {
            case ErrorSeverity::Info: return "INFO";
            case ErrorSeverity::Warning: return "WARNING";
            case ErrorSeverity::Error: return "ERROR";
            case ErrorSeverity::Critical: return "CRITICAL";
            case ErrorSeverity::Fatal: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    std::string Error::GetErrorCodeString() const
    {
        return ErrorHandling::GetErrorCodeString(m_Code);
    }

    namespace ErrorHandling
    {
        static ErrorHandler s_ErrorHandler = DefaultErrorHandler;

        void SetErrorHandler(ErrorHandler handler)
        {
            s_ErrorHandler = handler ? handler : DefaultErrorHandler;
        }

        ErrorHandler GetErrorHandler()
        {
            return s_ErrorHandler;
        }

        void DefaultErrorHandler(const Error& error)
        {
            // Log the error
            Error::LogError(error);
            
            // For fatal errors, break into debugger if available
            if (error.IsFatal())
            {
                PlatformUtils::BreakIntoDebugger();
            }
        }

        void Assert(bool condition, const std::string& message, const std::source_location& location)
        {
            if (!condition)
            {
                Error error(ErrorCode::Unknown, "Assertion failed: " + message, location, ErrorSeverity::Critical);
                s_ErrorHandler(error);
                throw error;
            }
        }

        void Verify(bool condition, const std::string& message, const std::source_location& location)
        {
            if (!condition)
            {
                Error error(ErrorCode::InvalidState, "Verification failed: " + message, location, ErrorSeverity::Error);
                s_ErrorHandler(error);
                throw error;
            }
        }

        std::string GetErrorCodeString(ErrorCode code)
        {
            static const std::unordered_map<ErrorCode, std::string> errorCodeStrings = {
                {ErrorCode::Success, "Success"},
                {ErrorCode::Unknown, "Unknown"},
                {ErrorCode::InvalidArgument, "InvalidArgument"},
                {ErrorCode::OutOfMemory, "OutOfMemory"},
                {ErrorCode::Timeout, "Timeout"},
                {ErrorCode::NotSupported, "NotSupported"},
                {ErrorCode::AlreadyInitialized, "AlreadyInitialized"},
                {ErrorCode::NotInitialized, "NotInitialized"},
                {ErrorCode::InvalidState, "InvalidState"},
                {ErrorCode::ResourceExhausted, "ResourceExhausted"},
                {ErrorCode::Cancelled, "Cancelled"},
                
                // System errors
                {ErrorCode::SystemError, "SystemError"},
                {ErrorCode::FileNotFound, "FileNotFound"},
                {ErrorCode::FileAccessDenied, "FileAccessDenied"},
                {ErrorCode::FileCorrupted, "FileCorrupted"},
                {ErrorCode::FileTooLarge, "FileTooLarge"},
                {ErrorCode::FileExists, "FileExists"},
                {ErrorCode::FileBusy, "FileBusy"},
                {ErrorCode::FileLocked, "FileLocked"},
                {ErrorCode::NetworkError, "NetworkError"},
                {ErrorCode::NetworkTimeout, "NetworkTimeout"},
                {ErrorCode::NetworkUnreachable, "NetworkUnreachable"},
                {ErrorCode::NetworkConnectionRefused, "NetworkConnectionRefused"},
                {ErrorCode::NetworkConnectionReset, "NetworkConnectionReset"},
                {ErrorCode::NetworkConnectionAborted, "NetworkConnectionAborted"},
                
                // Platform errors
                {ErrorCode::PlatformError, "PlatformError"},
                {ErrorCode::PlatformNotSupported, "PlatformNotSupported"},
                {ErrorCode::PlatformInitializationFailed, "PlatformInitializationFailed"},
                {ErrorCode::PlatformShutdownFailed, "PlatformShutdownFailed"},
                {ErrorCode::PlatformCapabilityNotAvailable, "PlatformCapabilityNotAvailable"},
                {ErrorCode::PlatformPermissionDenied, "PlatformPermissionDenied"},
                {ErrorCode::PlatformResourceUnavailable, "PlatformResourceUnavailable"},
                
                // Graphics errors
                {ErrorCode::GraphicsError, "GraphicsError"},
                {ErrorCode::WindowCreationFailed, "WindowCreationFailed"},
                {ErrorCode::ContextCreationFailed, "ContextCreationFailed"},
                {ErrorCode::ShaderCompilationFailed, "ShaderCompilationFailed"},
                {ErrorCode::TextureLoadFailed, "TextureLoadFailed"},
                {ErrorCode::RendererError, "RendererError"},
                {ErrorCode::DisplayError, "DisplayError"},
                {ErrorCode::MonitorError, "MonitorError"},
                {ErrorCode::CursorError, "CursorError"},
                {ErrorCode::ClipboardError, "ClipboardError"},
                
                // Audio errors
                {ErrorCode::AudioError, "AudioError"},
                {ErrorCode::AudioDeviceNotFound, "AudioDeviceNotFound"},
                {ErrorCode::AudioFormatNotSupported, "AudioFormatNotSupported"},
                {ErrorCode::AudioInitializationFailed, "AudioInitializationFailed"},
                {ErrorCode::AudioPlaybackError, "AudioPlaybackError"},
                {ErrorCode::AudioRecordingError, "AudioRecordingError"},
                
                // Input errors
                {ErrorCode::InputError, "InputError"},
                {ErrorCode::InputDeviceNotFound, "InputDeviceNotFound"},
                {ErrorCode::InputMappingError, "InputMappingError"},
                {ErrorCode::InputConfigurationError, "InputConfigurationError"},
                {ErrorCode::InputPermissionDenied, "InputPermissionDenied"},
                
                // Resource errors
                {ErrorCode::ResourceError, "ResourceError"},
                {ErrorCode::ResourceNotFound, "ResourceNotFound"},
                {ErrorCode::ResourceLoadFailed, "ResourceLoadFailed"},
                {ErrorCode::ResourceCorrupted, "ResourceCorrupted"},
                {ErrorCode::ResourceVersionMismatch, "ResourceVersionMismatch"},
                {ErrorCode::ResourceFormatNotSupported, "ResourceFormatNotSupported"},
                {ErrorCode::ResourceCompressionError, "ResourceCompressionError"},
                
                // Config errors
                {ErrorCode::ConfigError, "ConfigError"},
                {ErrorCode::ConfigFileNotFound, "ConfigFileNotFound"},
                {ErrorCode::ConfigParseError, "ConfigParseError"},
                {ErrorCode::ConfigValidationError, "ConfigValidationError"},
                {ErrorCode::ConfigSchemaError, "ConfigSchemaError"},
                {ErrorCode::ConfigVersionMismatch, "ConfigVersionMismatch"},
                
                // Event errors
                {ErrorCode::EventError, "EventError"},
                {ErrorCode::EventHandlerNotFound, "EventHandlerNotFound"},
                {ErrorCode::EventQueueFull, "EventQueueFull"},
                {ErrorCode::EventDispatchError, "EventDispatchError"},
                {ErrorCode::EventFilterError, "EventFilterError"},
                
                // Memory errors
                {ErrorCode::MemoryError, "MemoryError"},
                {ErrorCode::MemoryAllocationFailed, "MemoryAllocationFailed"},
                {ErrorCode::MemoryDeallocationFailed, "MemoryDeallocationFailed"},
                {ErrorCode::MemoryCorruption, "MemoryCorruption"},
                {ErrorCode::MemoryLeak, "MemoryLeak"},
                {ErrorCode::MemoryAlignmentError, "MemoryAlignmentError"},
                
                // Thread errors
                {ErrorCode::ThreadError, "ThreadError"},
                {ErrorCode::ThreadCreationFailed, "ThreadCreationFailed"},
                {ErrorCode::ThreadJoinFailed, "ThreadJoinFailed"},
                {ErrorCode::ThreadTerminationFailed, "ThreadTerminationFailed"},
                {ErrorCode::ThreadDeadlock, "ThreadDeadlock"},
                {ErrorCode::ThreadPermissionDenied, "ThreadPermissionDenied"},
                
                // Security errors
                {ErrorCode::SecurityError, "SecurityError"},
                {ErrorCode::SecurityPermissionDenied, "SecurityPermissionDenied"},
                {ErrorCode::SecurityAuthenticationFailed, "SecurityAuthenticationFailed"},
                {ErrorCode::SecurityAuthorizationFailed, "SecurityAuthorizationFailed"},
                {ErrorCode::SecurityIntegrityCheckFailed, "SecurityIntegrityCheckFailed"},
                
                // Performance errors
                {ErrorCode::PerformanceError, "PerformanceError"},
                {ErrorCode::PerformanceTimeout, "PerformanceTimeout"},
                {ErrorCode::PerformanceResourceExhausted, "PerformanceResourceExhausted"},
                {ErrorCode::PerformanceCapabilityExceeded, "PerformanceCapabilityExceeded"},
                
                // Debug errors
                {ErrorCode::DebugError, "DebugError"},
                {ErrorCode::DebugBreakpointError, "DebugBreakpointError"},
                {ErrorCode::DebugSymbolError, "DebugSymbolError"},
                {ErrorCode::DebugProfilerError, "DebugProfilerError"},
                
                // Hot reload errors
                {ErrorCode::HotReloadError, "HotReloadError"},
                {ErrorCode::HotReloadFileChanged, "HotReloadFileChanged"},
                {ErrorCode::HotReloadCompilationFailed, "HotReloadCompilationFailed"},
                {ErrorCode::HotReloadReloadFailed, "HotReloadReloadFailed"},
                {ErrorCode::HotReloadStateError, "HotReloadStateError"}
            };
            
            auto it = errorCodeStrings.find(code);
            return it != errorCodeStrings.end() ? it->second : "UnknownErrorCode";
        }

        std::string GetErrorCodeDescription(ErrorCode code)
        {
            static const std::unordered_map<ErrorCode, std::string> errorCodeDescriptions = {
                {ErrorCode::Success, "Operation completed successfully"},
                {ErrorCode::Unknown, "An unknown error occurred"},
                {ErrorCode::InvalidArgument, "One or more arguments are invalid"},
                {ErrorCode::OutOfMemory, "Insufficient memory to complete the operation"},
                {ErrorCode::Timeout, "Operation timed out"},
                {ErrorCode::NotSupported, "Operation is not supported on this platform"},
                {ErrorCode::AlreadyInitialized, "System is already initialized"},
                {ErrorCode::NotInitialized, "System is not initialized"},
                {ErrorCode::InvalidState, "System is in an invalid state for this operation"},
                {ErrorCode::ResourceExhausted, "System resources have been exhausted"},
                {ErrorCode::Cancelled, "Operation was cancelled"},
                
                // System errors
                {ErrorCode::SystemError, "A system-level error occurred"},
                {ErrorCode::FileNotFound, "The specified file was not found"},
                {ErrorCode::FileAccessDenied, "Access to the file was denied"},
                {ErrorCode::FileCorrupted, "The file is corrupted or invalid"},
                {ErrorCode::FileTooLarge, "The file is too large to process"},
                {ErrorCode::FileExists, "The file already exists"},
                {ErrorCode::FileBusy, "The file is currently in use"},
                {ErrorCode::FileLocked, "The file is locked by another process"},
                {ErrorCode::NetworkError, "A network error occurred"},
                {ErrorCode::NetworkTimeout, "Network operation timed out"},
                {ErrorCode::NetworkUnreachable, "Network destination is unreachable"},
                {ErrorCode::NetworkConnectionRefused, "Network connection was refused"},
                {ErrorCode::NetworkConnectionReset, "Network connection was reset"},
                {ErrorCode::NetworkConnectionAborted, "Network connection was aborted"},
                
                // Platform errors
                {ErrorCode::PlatformError, "A platform-specific error occurred"},
                {ErrorCode::PlatformNotSupported, "This platform is not supported"},
                {ErrorCode::PlatformInitializationFailed, "Platform initialization failed"},
                {ErrorCode::PlatformShutdownFailed, "Platform shutdown failed"},
                {ErrorCode::PlatformCapabilityNotAvailable, "Required platform capability is not available"},
                {ErrorCode::PlatformPermissionDenied, "Platform permission was denied"},
                {ErrorCode::PlatformResourceUnavailable, "Required platform resource is unavailable"},
                
                // Graphics errors
                {ErrorCode::GraphicsError, "A graphics-related error occurred"},
                {ErrorCode::WindowCreationFailed, "Failed to create window"},
                {ErrorCode::ContextCreationFailed, "Failed to create graphics context"},
                {ErrorCode::ShaderCompilationFailed, "Shader compilation failed"},
                {ErrorCode::TextureLoadFailed, "Failed to load texture"},
                {ErrorCode::RendererError, "Renderer encountered an error"},
                {ErrorCode::DisplayError, "Display-related error occurred"},
                {ErrorCode::MonitorError, "Monitor-related error occurred"},
                {ErrorCode::CursorError, "Cursor-related error occurred"},
                {ErrorCode::ClipboardError, "Clipboard-related error occurred"},
                
                // Audio errors
                {ErrorCode::AudioError, "An audio-related error occurred"},
                {ErrorCode::AudioDeviceNotFound, "Audio device not found"},
                {ErrorCode::AudioFormatNotSupported, "Audio format is not supported"},
                {ErrorCode::AudioInitializationFailed, "Audio system initialization failed"},
                {ErrorCode::AudioPlaybackError, "Audio playback error occurred"},
                {ErrorCode::AudioRecordingError, "Audio recording error occurred"},
                
                // Input errors
                {ErrorCode::InputError, "An input-related error occurred"},
                {ErrorCode::InputDeviceNotFound, "Input device not found"},
                {ErrorCode::InputMappingError, "Input mapping error occurred"},
                {ErrorCode::InputConfigurationError, "Input configuration error occurred"},
                {ErrorCode::InputPermissionDenied, "Input permission was denied"},
                
                // Resource errors
                {ErrorCode::ResourceError, "A resource-related error occurred"},
                {ErrorCode::ResourceNotFound, "Resource not found"},
                {ErrorCode::ResourceLoadFailed, "Failed to load resource"},
                {ErrorCode::ResourceCorrupted, "Resource is corrupted"},
                {ErrorCode::ResourceVersionMismatch, "Resource version mismatch"},
                {ErrorCode::ResourceFormatNotSupported, "Resource format is not supported"},
                {ErrorCode::ResourceCompressionError, "Resource compression error occurred"},
                
                // Config errors
                {ErrorCode::ConfigError, "A configuration-related error occurred"},
                {ErrorCode::ConfigFileNotFound, "Configuration file not found"},
                {ErrorCode::ConfigParseError, "Configuration parsing error occurred"},
                {ErrorCode::ConfigValidationError, "Configuration validation failed"},
                {ErrorCode::ConfigSchemaError, "Configuration schema error occurred"},
                {ErrorCode::ConfigVersionMismatch, "Configuration version mismatch"},
                
                // Event errors
                {ErrorCode::EventError, "An event system error occurred"},
                {ErrorCode::EventHandlerNotFound, "Event handler not found"},
                {ErrorCode::EventQueueFull, "Event queue is full"},
                {ErrorCode::EventDispatchError, "Event dispatch error occurred"},
                {ErrorCode::EventFilterError, "Event filter error occurred"},
                
                // Memory errors
                {ErrorCode::MemoryError, "A memory-related error occurred"},
                {ErrorCode::MemoryAllocationFailed, "Memory allocation failed"},
                {ErrorCode::MemoryDeallocationFailed, "Memory deallocation failed"},
                {ErrorCode::MemoryCorruption, "Memory corruption detected"},
                {ErrorCode::MemoryLeak, "Memory leak detected"},
                {ErrorCode::MemoryAlignmentError, "Memory alignment error occurred"},
                
                // Thread errors
                {ErrorCode::ThreadError, "A threading-related error occurred"},
                {ErrorCode::ThreadCreationFailed, "Thread creation failed"},
                {ErrorCode::ThreadJoinFailed, "Thread join failed"},
                {ErrorCode::ThreadTerminationFailed, "Thread termination failed"},
                {ErrorCode::ThreadDeadlock, "Thread deadlock detected"},
                {ErrorCode::ThreadPermissionDenied, "Thread permission denied"},
                
                // Security errors
                {ErrorCode::SecurityError, "A security-related error occurred"},
                {ErrorCode::SecurityPermissionDenied, "Security permission denied"},
                {ErrorCode::SecurityAuthenticationFailed, "Security authentication failed"},
                {ErrorCode::SecurityAuthorizationFailed, "Security authorization failed"},
                {ErrorCode::SecurityIntegrityCheckFailed, "Security integrity check failed"},
                
                // Performance errors
                {ErrorCode::PerformanceError, "A performance-related error occurred"},
                {ErrorCode::PerformanceTimeout, "Performance operation timed out"},
                {ErrorCode::PerformanceResourceExhausted, "Performance resources exhausted"},
                {ErrorCode::PerformanceCapabilityExceeded, "Performance capability exceeded"},
                
                // Debug errors
                {ErrorCode::DebugError, "A debug-related error occurred"},
                {ErrorCode::DebugBreakpointError, "Debug breakpoint error occurred"},
                {ErrorCode::DebugSymbolError, "Debug symbol error occurred"},
                {ErrorCode::DebugProfilerError, "Debug profiler error occurred"},
                
                // Hot reload errors
                {ErrorCode::HotReloadError, "A hot reload error occurred"},
                {ErrorCode::HotReloadFileChanged, "File changed during hot reload"},
                {ErrorCode::HotReloadCompilationFailed, "Hot reload compilation failed"},
                {ErrorCode::HotReloadReloadFailed, "Hot reload failed"},
                {ErrorCode::HotReloadStateError, "Hot reload state error occurred"}
            };
            
            auto it = errorCodeDescriptions.find(code);
            return it != errorCodeDescriptions.end() ? it->second : "No description available";
        }

        ErrorSeverity GetErrorCodeSeverity(ErrorCode code)
        {
            // Default severity mapping
            static const std::unordered_map<ErrorCode, ErrorSeverity> errorCodeSeverities = {
                {ErrorCode::Success, ErrorSeverity::Info},
                {ErrorCode::Unknown, ErrorSeverity::Error},
                {ErrorCode::InvalidArgument, ErrorSeverity::Error},
                {ErrorCode::OutOfMemory, ErrorSeverity::Critical},
                {ErrorCode::Timeout, ErrorSeverity::Warning},
                {ErrorCode::NotSupported, ErrorSeverity::Warning},
                {ErrorCode::AlreadyInitialized, ErrorSeverity::Warning},
                {ErrorCode::NotInitialized, ErrorSeverity::Error},
                {ErrorCode::InvalidState, ErrorSeverity::Error},
                {ErrorCode::ResourceExhausted, ErrorSeverity::Critical},
                {ErrorCode::Cancelled, ErrorSeverity::Info},
                
                // System errors - mostly Error level
                {ErrorCode::SystemError, ErrorSeverity::Error},
                {ErrorCode::FileNotFound, ErrorSeverity::Error},
                {ErrorCode::FileAccessDenied, ErrorSeverity::Error},
                {ErrorCode::FileCorrupted, ErrorSeverity::Critical},
                {ErrorCode::FileTooLarge, ErrorSeverity::Error},
                {ErrorCode::FileExists, ErrorSeverity::Warning},
                {ErrorCode::FileBusy, ErrorSeverity::Warning},
                {ErrorCode::FileLocked, ErrorSeverity::Warning},
                {ErrorCode::NetworkError, ErrorSeverity::Error},
                {ErrorCode::NetworkTimeout, ErrorSeverity::Warning},
                {ErrorCode::NetworkUnreachable, ErrorSeverity::Error},
                {ErrorCode::NetworkConnectionRefused, ErrorSeverity::Error},
                {ErrorCode::NetworkConnectionReset, ErrorSeverity::Error},
                {ErrorCode::NetworkConnectionAborted, ErrorSeverity::Error},
                
                // Platform errors - mostly Error level
                {ErrorCode::PlatformError, ErrorSeverity::Error},
                {ErrorCode::PlatformNotSupported, ErrorSeverity::Critical},
                {ErrorCode::PlatformInitializationFailed, ErrorSeverity::Critical},
                {ErrorCode::PlatformShutdownFailed, ErrorSeverity::Error},
                {ErrorCode::PlatformCapabilityNotAvailable, ErrorSeverity::Warning},
                {ErrorCode::PlatformPermissionDenied, ErrorSeverity::Error},
                {ErrorCode::PlatformResourceUnavailable, ErrorSeverity::Error},
                
                // Graphics errors - mostly Error level
                {ErrorCode::GraphicsError, ErrorSeverity::Error},
                {ErrorCode::WindowCreationFailed, ErrorSeverity::Critical},
                {ErrorCode::ContextCreationFailed, ErrorSeverity::Critical},
                {ErrorCode::ShaderCompilationFailed, ErrorSeverity::Error},
                {ErrorCode::TextureLoadFailed, ErrorSeverity::Error},
                {ErrorCode::RendererError, ErrorSeverity::Error},
                {ErrorCode::DisplayError, ErrorSeverity::Error},
                {ErrorCode::MonitorError, ErrorSeverity::Error},
                {ErrorCode::CursorError, ErrorSeverity::Warning},
                {ErrorCode::ClipboardError, ErrorSeverity::Warning},
                
                // Audio errors - mostly Error level
                {ErrorCode::AudioError, ErrorSeverity::Error},
                {ErrorCode::AudioDeviceNotFound, ErrorSeverity::Error},
                {ErrorCode::AudioFormatNotSupported, ErrorSeverity::Error},
                {ErrorCode::AudioInitializationFailed, ErrorSeverity::Error},
                {ErrorCode::AudioPlaybackError, ErrorSeverity::Error},
                {ErrorCode::AudioRecordingError, ErrorSeverity::Error},
                
                // Input errors - mostly Error level
                {ErrorCode::InputError, ErrorSeverity::Error},
                {ErrorCode::InputDeviceNotFound, ErrorSeverity::Error},
                {ErrorCode::InputMappingError, ErrorSeverity::Error},
                {ErrorCode::InputConfigurationError, ErrorSeverity::Error},
                {ErrorCode::InputPermissionDenied, ErrorSeverity::Error},
                
                // Resource errors - mostly Error level
                {ErrorCode::ResourceError, ErrorSeverity::Error},
                {ErrorCode::ResourceNotFound, ErrorSeverity::Error},
                {ErrorCode::ResourceLoadFailed, ErrorSeverity::Error},
                {ErrorCode::ResourceCorrupted, ErrorSeverity::Critical},
                {ErrorCode::ResourceVersionMismatch, ErrorSeverity::Error},
                {ErrorCode::ResourceFormatNotSupported, ErrorSeverity::Error},
                {ErrorCode::ResourceCompressionError, ErrorSeverity::Error},
                
                // Config errors - mostly Error level
                {ErrorCode::ConfigError, ErrorSeverity::Error},
                {ErrorCode::ConfigFileNotFound, ErrorSeverity::Error},
                {ErrorCode::ConfigParseError, ErrorSeverity::Error},
                {ErrorCode::ConfigValidationError, ErrorSeverity::Error},
                {ErrorCode::ConfigSchemaError, ErrorSeverity::Error},
                {ErrorCode::ConfigVersionMismatch, ErrorSeverity::Error},
                
                // Event errors - mostly Error level
                {ErrorCode::EventError, ErrorSeverity::Error},
                {ErrorCode::EventHandlerNotFound, ErrorSeverity::Error},
                {ErrorCode::EventQueueFull, ErrorSeverity::Warning},
                {ErrorCode::EventDispatchError, ErrorSeverity::Error},
                {ErrorCode::EventFilterError, ErrorSeverity::Error},
                
                // Memory errors - mostly Critical level
                {ErrorCode::MemoryError, ErrorSeverity::Critical},
                {ErrorCode::MemoryAllocationFailed, ErrorSeverity::Critical},
                {ErrorCode::MemoryDeallocationFailed, ErrorSeverity::Critical},
                {ErrorCode::MemoryCorruption, ErrorSeverity::Fatal},
                {ErrorCode::MemoryLeak, ErrorSeverity::Warning},
                {ErrorCode::MemoryAlignmentError, ErrorSeverity::Error},
                
                // Thread errors - mostly Error level
                {ErrorCode::ThreadError, ErrorSeverity::Error},
                {ErrorCode::ThreadCreationFailed, ErrorSeverity::Error},
                {ErrorCode::ThreadJoinFailed, ErrorSeverity::Error},
                {ErrorCode::ThreadTerminationFailed, ErrorSeverity::Error},
                {ErrorCode::ThreadDeadlock, ErrorSeverity::Critical},
                {ErrorCode::ThreadPermissionDenied, ErrorSeverity::Error},
                
                // Security errors - mostly Error level
                {ErrorCode::SecurityError, ErrorSeverity::Error},
                {ErrorCode::SecurityPermissionDenied, ErrorSeverity::Error},
                {ErrorCode::SecurityAuthenticationFailed, ErrorSeverity::Error},
                {ErrorCode::SecurityAuthorizationFailed, ErrorSeverity::Error},
                {ErrorCode::SecurityIntegrityCheckFailed, ErrorSeverity::Critical},
                
                // Performance errors - mostly Warning level
                {ErrorCode::PerformanceError, ErrorSeverity::Warning},
                {ErrorCode::PerformanceTimeout, ErrorSeverity::Warning},
                {ErrorCode::PerformanceResourceExhausted, ErrorSeverity::Error},
                {ErrorCode::PerformanceCapabilityExceeded, ErrorSeverity::Warning},
                
                // Debug errors - mostly Warning level
                {ErrorCode::DebugError, ErrorSeverity::Warning},
                {ErrorCode::DebugBreakpointError, ErrorSeverity::Warning},
                {ErrorCode::DebugSymbolError, ErrorSeverity::Warning},
                {ErrorCode::DebugProfilerError, ErrorSeverity::Warning},
                
                // Hot reload errors - mostly Warning level
                {ErrorCode::HotReloadError, ErrorSeverity::Warning},
                {ErrorCode::HotReloadFileChanged, ErrorSeverity::Info},
                {ErrorCode::HotReloadCompilationFailed, ErrorSeverity::Warning},
                {ErrorCode::HotReloadReloadFailed, ErrorSeverity::Warning},
                {ErrorCode::HotReloadStateError, ErrorSeverity::Error}
            };
            
            auto it = errorCodeSeverities.find(code);
            return it != errorCodeSeverities.end() ? it->second : ErrorSeverity::Error;
        }

        int GetLastSystemError()
        {
            #ifdef LT_PLATFORM_WINDOWS
                return GetLastError();
            #else
                return errno;
            #endif
        }

        std::string GetSystemErrorString(int errorCode)
        {
            #ifdef LT_PLATFORM_WINDOWS
                if (errorCode == 0) return "No error";
                
                LPSTR messageBuffer = nullptr;
                size_t size = FormatMessageA(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    errorCode,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPSTR)&messageBuffer,
                    0,
                    NULL
                );
                
                if (size > 0 && messageBuffer != nullptr)
                {
                    std::string message(messageBuffer);
                    LocalFree(messageBuffer);
                    
                    // Remove trailing whitespace and newlines
                    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
                    {
                        message.pop_back();
                    }
                    
                    return message;
                }
                else
                {
                    return "Unknown system error: " + std::to_string(errorCode);
                }
            #else
                if (errorCode == 0) return "No error";
                return strerror(errorCode);
            #endif
        }

        ErrorCode ConvertSystemError(int systemErrorCode)
        {
            #ifdef LT_PLATFORM_WINDOWS
                switch (systemErrorCode)
                {
                    case ERROR_SUCCESS: return ErrorCode::Success;
                    case ERROR_OUTOFMEMORY: return ErrorCode::OutOfMemory;
                    case ERROR_FILE_NOT_FOUND: return ErrorCode::FileNotFound;
                    case ERROR_ACCESS_DENIED: return ErrorCode::FileAccessDenied;
                    case ERROR_FILE_EXISTS: return ErrorCode::FileExists;
                    case ERROR_BUSY: return ErrorCode::FileBusy;
                    case ERROR_LOCK_VIOLATION: return ErrorCode::FileLocked;
                    case ERROR_TIMEOUT: return ErrorCode::Timeout;
                    case ERROR_NOT_SUPPORTED: return ErrorCode::NotSupported;
                    case ERROR_INVALID_PARAMETER: return ErrorCode::InvalidArgument;
                    case ERROR_INVALID_STATE: return ErrorCode::InvalidState;
                    case ERROR_CANCELLED: return ErrorCode::Cancelled;
                    default: return ErrorCode::SystemError;
                }
            #else
                switch (systemErrorCode)
                {
                    case 0: return ErrorCode::Success;
                    case ENOMEM: return ErrorCode::OutOfMemory;
                    case ENOENT: return ErrorCode::FileNotFound;
                    case EACCES: return ErrorCode::FileAccessDenied;
                    case EEXIST: return ErrorCode::FileExists;
                    case EBUSY: return ErrorCode::FileBusy;
                    case EAGAIN: return ErrorCode::FileLocked;
                    case ETIMEDOUT: return ErrorCode::Timeout;
                    case ENOSYS: return ErrorCode::NotSupported;
                    case EINVAL: return ErrorCode::InvalidArgument;
                    case EPERM: return ErrorCode::PlatformPermissionDenied;
                    case ENOSPC: return ErrorCode::ResourceExhausted;
                    default: return ErrorCode::SystemError;
                }
            #endif
        }
    }
}