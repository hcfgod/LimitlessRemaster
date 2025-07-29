#pragma once

#include <exception>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>

// For MSVC, we need to check _MSVC_LANG instead of __cplusplus
#if (defined(_MSC_VER) && _MSVC_LANG >= 202002L) || (!defined(_MSC_VER) && __cplusplus >= 202002L)
    #include <source_location>
#elif defined(__has_include) && __has_include(<experimental/source_location>)
    #include <experimental/source_location>
    namespace std {
        using source_location = std::experimental::source_location;
    }
#else
    // Fallback for older compilers
    namespace std {
        struct source_location {
            static constexpr source_location current() noexcept { return {}; }
            constexpr const char* file_name() const noexcept { return "unknown"; }
            constexpr uint_least32_t line() const noexcept { return 0; }
            constexpr uint_least32_t column() const noexcept { return 0; }
            constexpr const char* function_name() const noexcept { return "unknown"; }
        };
    }
#endif

namespace Limitless
{
    // Forward declaration
    struct PlatformInfo;

    enum class ErrorCode
    {
        // General errors
        Success = 0,
        Unknown = 1,
        InvalidArgument = 2,
        OutOfMemory = 3,
        Timeout = 4,
        NotSupported = 5,
        AlreadyInitialized = 6,
        NotInitialized = 7,
        InvalidState = 8,
        ResourceExhausted = 9,
        Cancelled = 10,
        
        // System errors
        SystemError = 100,
        FileNotFound = 101,
        FileAccessDenied = 102,
        FileCorrupted = 103,
        FileTooLarge = 104,
        FileExists = 105,
        FileBusy = 106,
        FileLocked = 107,
        NetworkError = 108,
        NetworkTimeout = 109,
        NetworkUnreachable = 110,
        NetworkConnectionRefused = 111,
        NetworkConnectionReset = 112,
        NetworkConnectionAborted = 113,
        
        // Platform-specific errors
        PlatformError = 200,
        PlatformNotSupported = 201,
        PlatformInitializationFailed = 202,
        PlatformShutdownFailed = 203,
        PlatformCapabilityNotAvailable = 204,
        PlatformPermissionDenied = 205,
        PlatformResourceUnavailable = 206,
        
        // Windows-specific errors
        WindowsError = 300,
        WindowsApiError = 301,
        WindowsRegistryError = 302,
        WindowsServiceError = 303,
        WindowsSecurityError = 304,
        
        // macOS-specific errors
        MacOSError = 400,
        MacOSApiError = 401,
        MacOSPermissionError = 402,
        MacOSEntitlementError = 403,
        
        // Linux-specific errors
        LinuxError = 500,
        LinuxApiError = 501,
        LinuxPermissionError = 502,
        LinuxKernelError = 503,
        
        // Graphics/Window errors
        GraphicsError = 600,
        WindowCreationFailed = 601,
        ContextCreationFailed = 602,
        ShaderCompilationFailed = 603,
        TextureLoadFailed = 604,
        RendererError = 605,
        DisplayError = 606,
        MonitorError = 607,
        CursorError = 608,
        ClipboardError = 609,
        
        // Audio errors
        AudioError = 700,
        AudioDeviceNotFound = 701,
        AudioFormatNotSupported = 702,
        AudioInitializationFailed = 703,
        AudioPlaybackError = 704,
        AudioRecordingError = 705,
        
        // Input errors
        InputError = 800,
        InputDeviceNotFound = 801,
        InputMappingError = 802,
        InputConfigurationError = 803,
        InputPermissionDenied = 804,
        
        // Resource errors
        ResourceError = 900,
        ResourceNotFound = 901,
        ResourceLoadFailed = 902,
        ResourceCorrupted = 903,
        ResourceVersionMismatch = 904,
        ResourceFormatNotSupported = 905,
        ResourceCompressionError = 906,
        
        // Configuration errors
        ConfigError = 1000,
        ConfigFileNotFound = 1001,
        ConfigParseError = 1002,
        ConfigValidationError = 1003,
        ConfigSchemaError = 1004,
        ConfigVersionMismatch = 1005,
        
        // Event system errors
        EventError = 1100,
        EventHandlerNotFound = 1101,
        EventQueueFull = 1102,
        EventDispatchError = 1103,
        EventFilterError = 1104,
        
        // Memory errors
        MemoryError = 1200,
        MemoryAllocationFailed = 1201,
        MemoryDeallocationFailed = 1202,
        MemoryCorruption = 1203,
        MemoryLeak = 1204,
        MemoryAlignmentError = 1205,
        
        // Threading errors
        ThreadError = 1300,
        ThreadCreationFailed = 1301,
        ThreadJoinFailed = 1302,
        ThreadTerminationFailed = 1303,
        ThreadDeadlock = 1304,
        ThreadPermissionDenied = 1305,
        
        // Security errors
        SecurityError = 1400,
        SecurityPermissionDenied = 1401,
        SecurityAuthenticationFailed = 1402,
        SecurityAuthorizationFailed = 1403,
        SecurityIntegrityCheckFailed = 1404,
        
        // Performance errors
        PerformanceError = 1500,
        PerformanceTimeout = 1501,
        PerformanceResourceExhausted = 1502,
        PerformanceCapabilityExceeded = 1503,
        
        // Debug/Development errors
        DebugError = 1600,
        DebugBreakpointError = 1601,
        DebugSymbolError = 1602,
        DebugProfilerError = 1603,
        
        // Hot reload errors
        HotReloadError = 1700,
        HotReloadFileChanged = 1701,
        HotReloadCompilationFailed = 1702,
        HotReloadReloadFailed = 1703,
        HotReloadStateError = 1704
    };

    // Error severity levels
    enum class ErrorSeverity
    {
        Info = 0,
        Warning = 1,
        Error = 2,
        Critical = 3,
        Fatal = 4
    };

    // Error context information
    struct ErrorContext
    {
        std::string functionName;
        std::string className;
        std::string moduleName;
        std::string threadId;
        uint64_t timestamp;
        std::string platformInfo;
        std::string systemInfo;
        std::unordered_map<std::string, std::string> additionalData;
    };

    class Error : public std::exception
    {
    public:
        Error(ErrorCode code, const std::string& message, 
              const std::source_location& location,
              ErrorSeverity severity = ErrorSeverity::Error);
        
        ErrorCode GetCode() const { return m_Code; }
        const std::string& GetErrorMessage() const { return m_Message; }
        const std::string& GetLocation() const { return m_Location; }
        ErrorSeverity GetSeverity() const { return m_Severity; }
        const ErrorContext& GetContext() const { return m_Context; }
        
        const char* what() const noexcept override;
        
        // Helper methods
        bool IsSuccess() const { return m_Code == ErrorCode::Success; }
        bool IsFailure() const { return m_Code != ErrorCode::Success; }
        bool IsCritical() const { return m_Severity >= ErrorSeverity::Critical; }
        bool IsFatal() const { return m_Severity == ErrorSeverity::Fatal; }
        
        // Convert to string
        std::string ToString() const;
        std::string ToDetailedString() const;
        
        // Log error using the logging system
        static void LogError(const Error& error);
        
        // Add context information
        void AddContext(const std::string& key, const std::string& value);
        void SetFunctionName(const std::string& functionName);
        void SetClassName(const std::string& className);
        void SetModuleName(const std::string& moduleName);
        
        // Platform-specific error information
        void SetPlatformInfo(const PlatformInfo& platformInfo);
        void SetSystemErrorCode(int systemErrorCode);
        int GetSystemErrorCode() const { return m_SystemErrorCode; }
        
        // Context access methods
        std::string GetContextValue(const std::string& key) const;

    private:
        ErrorCode m_Code;
        std::string m_Message;
        std::string m_Location;
        ErrorSeverity m_Severity;
        ErrorContext m_Context;
        int m_SystemErrorCode;
        mutable std::string m_WhatBuffer;
        
        void BuildContext();
        std::string GetSeverityString() const;
        std::string GetErrorCodeString() const;
    };

    // Specific error types
    class SystemError : public Error
    {
    public:
        SystemError(const std::string& message, 
                   const std::source_location& location,
                   ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::SystemError, message, location, severity) {}
    };

    class PlatformError : public Error
    {
    public:
        PlatformError(const std::string& message, 
                     const std::source_location& location,
                     ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::PlatformError, message, location, severity) {}
    };

    class GraphicsError : public Error
    {
    public:
        GraphicsError(const std::string& message, 
                     const std::source_location& location,
                     ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::GraphicsError, message, location, severity) {}
    };

    class ResourceError : public Error
    {
    public:
        ResourceError(const std::string& message, 
                     const std::source_location& location,
                     ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::ResourceError, message, location, severity) {}
    };

    class ConfigError : public Error
    {
    public:
        ConfigError(const std::string& message, 
                   const std::source_location& location,
                   ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::ConfigError, message, location, severity) {}
    };

    class MemoryError : public Error
    {
    public:
        MemoryError(const std::string& message, 
                   const std::source_location& location,
                   ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::MemoryError, message, location, severity) {}
    };

    class ThreadError : public Error
    {
    public:
        ThreadError(const std::string& message, 
                   const std::source_location& location,
                   ErrorSeverity severity = ErrorSeverity::Error)
            : Error(ErrorCode::ThreadError, message, location, severity) {}
    };

    // Result class for error handling
    template<typename T>
    class Result
    {
    public:
        // Success constructor
        Result(const T& value) : m_Value(value), m_Error(ErrorCode::Success, "", std::source_location::current()) {}
        Result(T&& value) : m_Value(std::move(value)), m_Error(ErrorCode::Success, "", std::source_location::current()) {}
        
        // Error constructor
        Result(const Error& error) : m_Error(error) {}
        Result(ErrorCode code, const std::string& message) : m_Error(code, message, std::source_location::current()) {}
        
        // Check if successful
        bool IsSuccess() const { return m_Error.IsSuccess(); }
        bool IsFailure() const { return m_Error.IsFailure(); }
        
        // Get value (throws if error)
        T& GetValue() 
        { 
            if (IsFailure()) throw m_Error;
            return m_Value;
        }
        
        const T& GetValue() const 
        { 
            if (IsFailure()) throw m_Error;
            return m_Value;
        }
        
        // Get error
        const Error& GetError() const { return m_Error; }
        
        // Safe value access
        T* GetValuePtr() { return IsSuccess() ? &m_Value : nullptr; }
        const T* GetValuePtr() const { return IsSuccess() ? &m_Value : nullptr; }
        
        // Value or default
        T GetValueOr(const T& defaultValue) const { return IsSuccess() ? m_Value : defaultValue; }
        
        // Value or throw
        T GetValueOrThrow() const 
        { 
            if (IsFailure()) throw m_Error;
            return m_Value;
        }

    private:
        T m_Value;
        Error m_Error;
    };

    // Specialization for void
    template<>
    class Result<void>
    {
    public:
        // Success constructor
        Result() : m_Error(ErrorCode::Success, "", std::source_location::current()) {}
        
        // Error constructor
        Result(const Error& error) : m_Error(error) {}
        Result(ErrorCode code, const std::string& message) : m_Error(code, message, std::source_location::current()) {}
        
        // Check if successful
        bool IsSuccess() const { return m_Error.IsSuccess(); }
        bool IsFailure() const { return m_Error.IsFailure(); }
        
        // Get error
        const Error& GetError() const { return m_Error; }
        
        // Throw if error
        void GetValue() const { if (IsFailure()) throw m_Error; }

    private:
        Error m_Error;
    };

    // Error handling utilities
    namespace ErrorHandling
    {
        // Error handler function type
        using ErrorHandler = std::function<void(const Error&)>;
        
        // Set global error handler
        void SetErrorHandler(ErrorHandler handler);
        
        // Get current error handler
        ErrorHandler GetErrorHandler();
        
        // Default error handling
        void DefaultErrorHandler(const Error& error);
        
        // Assert with error
        void Assert(bool condition, const std::string& message, 
                   const std::source_location& location = std::source_location::current());
        
        // Verify with error
        void Verify(bool condition, const std::string& message, 
                   const std::source_location& location = std::source_location::current());
        
        // Try-catch wrapper
        template<typename Func>
        auto Try(Func&& func) -> Result<decltype(func())>
        {
            try
            {
                return Result<decltype(func())>(func());
            }
            catch (const Error& error)
            {
                return Result<decltype(func())>(error);
            }
            catch (const std::exception& e)
            {
                return Result<decltype(func())>(Error(ErrorCode::Unknown, e.what(), std::source_location::current()));
            }
            catch (...)
            {
                return Result<decltype(func())>(Error(ErrorCode::Unknown, "Unknown exception", std::source_location::current()));
            }
        }
        
        // Error code utilities
        std::string GetErrorCodeString(ErrorCode code);
        std::string GetErrorCodeDescription(ErrorCode code);
        ErrorSeverity GetErrorCodeSeverity(ErrorCode code);
        
        // System error utilities
        int GetLastSystemError();
        std::string GetSystemErrorString(int errorCode);
        ErrorCode ConvertSystemError(int systemErrorCode);
    }

    // Convenience macros
    #define LT_ASSERT(condition, message) \
        Limitless::ErrorHandling::Assert(condition, message)
    
    #define LT_VERIFY(condition, message) \
        Limitless::ErrorHandling::Verify(condition, message)
    
    #define LT_THROW_ERROR(code, message) \
        throw Limitless::Error(code, message, std::source_location::current())
    
    #define LT_THROW_SYSTEM_ERROR(message) \
        throw Limitless::SystemError(message, std::source_location::current())
    
    #define LT_THROW_PLATFORM_ERROR(message) \
        throw Limitless::PlatformError(message, std::source_location::current())
    
    #define LT_THROW_GRAPHICS_ERROR(message) \
        throw Limitless::GraphicsError(message, std::source_location::current())
    
    #define LT_THROW_RESOURCE_ERROR(message) \
        throw Limitless::ResourceError(message, std::source_location::current())
    
    #define LT_THROW_CONFIG_ERROR(message) \
        throw Limitless::ConfigError(message, std::source_location::current())
    
    #define LT_THROW_MEMORY_ERROR(message) \
        throw Limitless::MemoryError(message, std::source_location::current())
    
    #define LT_THROW_THREAD_ERROR(message) \
        throw Limitless::ThreadError(message, std::source_location::current())
    
    #define LT_TRY(expr) \
        Limitless::ErrorHandling::Try([&]() { return expr; })
    
    #define LT_TRY_VOID(expr) \
        Limitless::ErrorHandling::Try([&]() { expr; return; })
    
    #define LT_RETURN_IF_ERROR(result) \
        if (result.IsFailure()) return result.GetError();
    
    #define LT_RETURN_IF_ERROR_VOID(result) \
        if (result.IsFailure()) return;
}