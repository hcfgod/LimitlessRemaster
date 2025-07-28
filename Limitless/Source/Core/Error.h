#pragma once

#include <exception>
#include <string>
#include <source_location>
#include <functional>

namespace Limitless
{
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
        
        // System errors
        SystemError = 100,
        FileNotFound = 101,
        FileAccessDenied = 102,
        FileCorrupted = 103,
        NetworkError = 104,
        
        // Graphics/Window errors
        GraphicsError = 200,
        WindowCreationFailed = 201,
        ContextCreationFailed = 202,
        ShaderCompilationFailed = 203,
        TextureLoadFailed = 204,
        RendererError = 205,
        
        // Audio errors
        AudioError = 300,
        AudioDeviceNotFound = 301,
        AudioFormatNotSupported = 302,
        
        // Input errors
        InputError = 400,
        InputDeviceNotFound = 401,
        InputMappingError = 402,
        
        // Resource errors
        ResourceError = 500,
        ResourceNotFound = 501,
        ResourceLoadFailed = 502,
        ResourceCorrupted = 503,
        
        // Configuration errors
        ConfigError = 600,
        ConfigFileNotFound = 601,
        ConfigParseError = 602,
        ConfigValidationError = 603
    };

    class Error : public std::exception
    {
    public:
        Error(ErrorCode code, const std::string& message, 
              const std::source_location& location = std::source_location::current());
        
        ErrorCode GetCode() const { return m_Code; }
        const std::string& GetMessage() const { return m_Message; }
        const std::string& GetLocation() const { return m_Location; }
        
        const char* what() const noexcept override;
        
        // Helper methods
        bool IsSuccess() const { return m_Code == ErrorCode::Success; }
        bool IsFailure() const { return m_Code != ErrorCode::Success; }
        
        // Convert to string
        std::string ToString() const;

    private:
        ErrorCode m_Code;
        std::string m_Message;
        std::string m_Location;
        mutable std::string m_WhatBuffer;
    };

    // Specific error types
    class SystemError : public Error
    {
    public:
        SystemError(const std::string& message, 
                   const std::source_location& location = std::source_location::current())
            : Error(ErrorCode::SystemError, message, location) {}
    };

    class GraphicsError : public Error
    {
    public:
        GraphicsError(const std::string& message, 
                     const std::source_location& location = std::source_location::current())
            : Error(ErrorCode::GraphicsError, message, location) {}
    };

    class ResourceError : public Error
    {
    public:
        ResourceError(const std::string& message, 
                     const std::source_location& location = std::source_location::current())
            : Error(ErrorCode::ResourceError, message, location) {}
    };

    class ConfigError : public Error
    {
    public:
        ConfigError(const std::string& message, 
                   const std::source_location& location = std::source_location::current())
            : Error(ErrorCode::ConfigError, message, location) {}
    };

    // Result class for error handling
    template<typename T>
    class Result
    {
    public:
        // Success constructor
        Result(const T& value) : m_Value(value), m_Error(ErrorCode::Success, "") {}
        Result(T&& value) : m_Value(std::move(value)), m_Error(ErrorCode::Success, "") {}
        
        // Error constructor
        Result(const Error& error) : m_Error(error) {}
        Result(ErrorCode code, const std::string& message) : m_Error(code, message) {}
        
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
        Result() : m_Error(ErrorCode::Success, "") {}
        
        // Error constructor
        Result(const Error& error) : m_Error(error) {}
        Result(ErrorCode code, const std::string& message) : m_Error(code, message) {}
        
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
                return Result<decltype(func())>(ErrorCode::Unknown, e.what());
            }
            catch (...)
            {
                return Result<decltype(func())>(ErrorCode::Unknown, "Unknown exception");
            }
        }
    }

    // Convenience macros
    #define LT_ASSERT(condition, message) \
        Limitless::ErrorHandling::Assert(condition, message)
    
    #define LT_THROW_ERROR(code, message) \
        throw Limitless::Error(code, message)
    
    #define LT_THROW_SYSTEM_ERROR(message) \
        throw Limitless::SystemError(message)
    
    #define LT_THROW_GRAPHICS_ERROR(message) \
        throw Limitless::GraphicsError(message)
    
    #define LT_THROW_RESOURCE_ERROR(message) \
        throw Limitless::ResourceError(message)
    
    #define LT_TRY(expr) \
        Limitless::ErrorHandling::Try([&]() { return expr; })
}