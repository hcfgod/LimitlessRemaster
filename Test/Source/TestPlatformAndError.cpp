#include <doctest/doctest.h>
#include "Platform/Platform.h"
#include "Core/Error.h"
#include <iostream>

TEST_SUITE("Platform Detection")
{
    TEST_CASE("Platform detection initialization")
    {
        // Initialize platform detection
        Limitless::PlatformDetection::Initialize();
        
        const auto& platformInfo = Limitless::PlatformDetection::GetPlatformInfo();
        
        // Basic platform info should be available
        CHECK_FALSE(platformInfo.platformName.empty());
        CHECK_FALSE(platformInfo.architectureName.empty());
        CHECK_FALSE(platformInfo.compilerName.empty());
        CHECK_FALSE(platformInfo.osName.empty());
        
        // Platform should be detected
        CHECK(platformInfo.platform != Limitless::Platform::Unknown);
        CHECK(platformInfo.architecture != Limitless::Architecture::Unknown);
        CHECK(platformInfo.compiler != Limitless::Compiler::Unknown);
    }
    
    TEST_CASE("Platform-specific checks")
    {
        // Test platform-specific boolean checks
        #ifdef LT_PLATFORM_WINDOWS
            CHECK(Limitless::PlatformDetection::IsWindows());
            CHECK_FALSE(Limitless::PlatformDetection::IsMacOS());
            CHECK_FALSE(Limitless::PlatformDetection::IsLinux());
        #elif defined(LT_PLATFORM_MACOS)
            CHECK_FALSE(Limitless::PlatformDetection::IsWindows());
            CHECK(Limitless::PlatformDetection::IsMacOS());
            CHECK_FALSE(Limitless::PlatformDetection::IsLinux());
        #elif defined(LT_PLATFORM_LINUX)
            CHECK_FALSE(Limitless::PlatformDetection::IsWindows());
            CHECK_FALSE(Limitless::PlatformDetection::IsMacOS());
            CHECK(Limitless::PlatformDetection::IsLinux());
        #endif
        
        // Test architecture checks
        #ifdef LT_ARCHITECTURE_X64
            CHECK(Limitless::PlatformDetection::IsX64());
            CHECK_FALSE(Limitless::PlatformDetection::IsX86());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM32());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM64());
        #elif defined(LT_ARCHITECTURE_X86)
            CHECK_FALSE(Limitless::PlatformDetection::IsX64());
            CHECK(Limitless::PlatformDetection::IsX86());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM32());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM64());
        #elif defined(LT_ARCHITECTURE_ARM64)
            CHECK_FALSE(Limitless::PlatformDetection::IsX64());
            CHECK_FALSE(Limitless::PlatformDetection::IsX86());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM32());
            CHECK(Limitless::PlatformDetection::IsARM64());
        #elif defined(LT_ARCHITECTURE_ARM32)
            CHECK_FALSE(Limitless::PlatformDetection::IsX64());
            CHECK_FALSE(Limitless::PlatformDetection::IsX86());
            CHECK(Limitless::PlatformDetection::IsARM32());
            CHECK_FALSE(Limitless::PlatformDetection::IsARM64());
        #endif
        
        // Test compiler checks
        #ifdef LT_COMPILER_MSVC
            CHECK(Limitless::PlatformDetection::IsMSVC());
            CHECK_FALSE(Limitless::PlatformDetection::IsGCC());
            CHECK_FALSE(Limitless::PlatformDetection::IsClang());
            CHECK_FALSE(Limitless::PlatformDetection::IsAppleClang());
        #elif defined(LT_COMPILER_GCC)
            CHECK_FALSE(Limitless::PlatformDetection::IsMSVC());
            CHECK(Limitless::PlatformDetection::IsGCC());
            CHECK_FALSE(Limitless::PlatformDetection::IsClang());
            CHECK_FALSE(Limitless::PlatformDetection::IsAppleClang());
        #elif defined(LT_COMPILER_CLANG)
            CHECK_FALSE(Limitless::PlatformDetection::IsMSVC());
            CHECK_FALSE(Limitless::PlatformDetection::IsGCC());
            CHECK(Limitless::PlatformDetection::IsClang());
            CHECK_FALSE(Limitless::PlatformDetection::IsAppleClang());
        #elif defined(LT_COMPILER_APPLE_CLANG)
            CHECK_FALSE(Limitless::PlatformDetection::IsMSVC());
            CHECK_FALSE(Limitless::PlatformDetection::IsGCC());
            CHECK_FALSE(Limitless::PlatformDetection::IsClang());
            CHECK(Limitless::PlatformDetection::IsAppleClang());
        #endif
    }
    
    TEST_CASE("System capabilities")
    {
        const auto& platformInfo = Limitless::PlatformDetection::GetPlatformInfo();
        
        // CPU count should be reasonable
        CHECK(platformInfo.capabilities.cpuCount > 0);
        CHECK(platformInfo.capabilities.cpuCount <= 128); // Reasonable upper limit
        
        // Memory should be reasonable
        CHECK(platformInfo.capabilities.totalMemory > 0);
        CHECK(platformInfo.capabilities.totalMemory <= 1024ULL * 1024 * 1024 * 1024); // 1TB upper limit
        CHECK(platformInfo.capabilities.availableMemory <= platformInfo.capabilities.totalMemory);
    }
    
    TEST_CASE("Platform utilities")
    {
        // Path separator should be valid
        std::string separator = Limitless::PlatformUtils::GetPathSeparator();
        CHECK(separator == "\\" || separator == "/");
        
        // Process and thread IDs should be valid
        CHECK(Limitless::PlatformUtils::GetCurrentProcessId() > 0);
        CHECK(Limitless::PlatformUtils::GetCurrentThreadId() > 0);
        
        // Time functions should return reasonable values
        uint64_t highResTime = Limitless::PlatformUtils::GetHighResolutionTime();
        uint64_t systemTime = Limitless::PlatformUtils::GetSystemTime();
        CHECK(highResTime > 0);
        CHECK(systemTime > 0);
        
        // Path utilities should work
        std::string joinedPath = Limitless::PlatformUtils::JoinPath("test", "file.txt");
        CHECK_FALSE(joinedPath.empty());
        
        std::string dirName = Limitless::PlatformUtils::GetDirectoryName(joinedPath);
        std::string fileName = Limitless::PlatformUtils::GetFileName(joinedPath);
        std::string extension = Limitless::PlatformUtils::GetFileExtension(joinedPath);
        
        CHECK_FALSE(dirName.empty());
        CHECK_FALSE(fileName.empty());
        CHECK(extension == ".txt");
    }
}

TEST_SUITE("Error Handling")
{
    TEST_CASE("Basic error creation")
    {
        Limitless::Error error(Limitless::ErrorCode::FileNotFound, "Test error", std::source_location::current());
        
        CHECK(error.GetCode() == Limitless::ErrorCode::FileNotFound);
        		CHECK(error.GetErrorMessage() == "Test error");
        CHECK_FALSE(error.IsSuccess());
        CHECK(error.IsFailure());
        CHECK_FALSE(error.IsCritical());
        CHECK_FALSE(error.IsFatal());
    }
    
    TEST_CASE("Error severity levels")
    {
        Limitless::Error infoError(Limitless::ErrorCode::Cancelled, "Info", std::source_location::current(), Limitless::ErrorSeverity::Info);
        Limitless::Error warningError(Limitless::ErrorCode::FileExists, "Warning", std::source_location::current(), Limitless::ErrorSeverity::Warning);
        Limitless::Error errorError(Limitless::ErrorCode::FileNotFound, "Error", std::source_location::current(), Limitless::ErrorSeverity::Error);
        Limitless::Error criticalError(Limitless::ErrorCode::OutOfMemory, "Critical", std::source_location::current(), Limitless::ErrorSeverity::Critical);
        Limitless::Error fatalError(Limitless::ErrorCode::MemoryCorruption, "Fatal", std::source_location::current(), Limitless::ErrorSeverity::Fatal);
        
        CHECK_FALSE(infoError.IsCritical());
        CHECK_FALSE(warningError.IsCritical());
        CHECK_FALSE(errorError.IsCritical());
        CHECK(criticalError.IsCritical());
        CHECK(fatalError.IsCritical());
        
        CHECK_FALSE(infoError.IsFatal());
        CHECK_FALSE(warningError.IsFatal());
        CHECK_FALSE(errorError.IsFatal());
        CHECK_FALSE(criticalError.IsFatal());
        CHECK(fatalError.IsFatal());
    }
    
    TEST_CASE("Error context")
    {
        Limitless::Error error(Limitless::ErrorCode::InvalidArgument, "Test error", std::source_location::current());
        
        error.SetFunctionName("TestFunction");
        error.SetClassName("TestClass");
        error.SetModuleName("TestModule");
        error.AddContext("TestKey", "TestValue");
        
        const auto& context = error.GetContext();
        CHECK(context.functionName == "TestFunction");
        CHECK(context.className == "TestClass");
        CHECK(context.moduleName == "TestModule");
        CHECK(error.GetContextValue("TestKey") == "TestValue");
        CHECK(context.timestamp > 0);
        CHECK_FALSE(context.threadId.empty());
    }
    
    TEST_CASE("Specific error types")
    {
        Limitless::SystemError systemError("System error", std::source_location::current());
        Limitless::PlatformError platformError("Platform error", std::source_location::current());
        Limitless::GraphicsError graphicsError("Graphics error", std::source_location::current());
        Limitless::ResourceError resourceError("Resource error", std::source_location::current());
        Limitless::ConfigError configError("Config error", std::source_location::current());
        Limitless::MemoryError memoryError("Memory error", std::source_location::current());
        Limitless::ThreadError threadError("Thread error", std::source_location::current());
        
        CHECK(systemError.GetCode() == Limitless::ErrorCode::SystemError);
        CHECK(platformError.GetCode() == Limitless::ErrorCode::PlatformError);
        CHECK(graphicsError.GetCode() == Limitless::ErrorCode::GraphicsError);
        CHECK(resourceError.GetCode() == Limitless::ErrorCode::ResourceError);
        CHECK(configError.GetCode() == Limitless::ErrorCode::ConfigError);
        CHECK(memoryError.GetCode() == Limitless::ErrorCode::MemoryError);
        CHECK(threadError.GetCode() == Limitless::ErrorCode::ThreadError);
    }
    
    TEST_CASE("Result class")
    {
        // Success result
        Limitless::Result<int> successResult(42);
        CHECK(successResult.IsSuccess());
        CHECK_FALSE(successResult.IsFailure());
        CHECK(successResult.GetValue() == 42);
        CHECK(successResult.GetValueOr(0) == 42);
        
        int* valuePtr = successResult.GetValuePtr();
        CHECK(valuePtr != nullptr);
        CHECK(*valuePtr == 42);
        
        // Error result
        Limitless::Result<int> errorResult(Limitless::ErrorCode::FileNotFound, "File not found");
        CHECK_FALSE(errorResult.IsSuccess());
        CHECK(errorResult.IsFailure());
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::FileNotFound);
        CHECK(errorResult.GetValueOr(0) == 0);
        
        valuePtr = errorResult.GetValuePtr();
        CHECK(valuePtr == nullptr);
        
        // Error result should throw when accessing value
        CHECK_THROWS_AS(errorResult.GetValue(), Limitless::Error);
    }
    
    TEST_CASE("Result void specialization")
    {
        // Success result
        Limitless::Result<void> successResult;
        CHECK(successResult.IsSuccess());
        CHECK_FALSE(successResult.IsFailure());
        
        // Error result
        Limitless::Result<void> errorResult(Limitless::ErrorCode::InvalidState, "Invalid state");
        CHECK_FALSE(errorResult.IsSuccess());
        CHECK(errorResult.IsFailure());
        
        // Error result should throw when accessing value
        CHECK_THROWS_AS(errorResult.GetValue(), Limitless::Error);
    }
    
    TEST_CASE("Error handling utilities")
    {
        // Test error code utilities
        std::string codeString = Limitless::ErrorHandling::GetErrorCodeString(Limitless::ErrorCode::FileNotFound);
        std::string description = Limitless::ErrorHandling::GetErrorCodeDescription(Limitless::ErrorCode::FileNotFound);
        Limitless::ErrorSeverity severity = Limitless::ErrorHandling::GetErrorCodeSeverity(Limitless::ErrorCode::FileNotFound);
        
        CHECK_FALSE(codeString.empty());
        CHECK_FALSE(description.empty());
        CHECK(severity == Limitless::ErrorSeverity::Error);
        
        // Test system error utilities
        int lastError = Limitless::ErrorHandling::GetLastSystemError();
        std::string systemErrorString = Limitless::ErrorHandling::GetSystemErrorString(lastError);
        
        CHECK_FALSE(systemErrorString.empty());
        
        // Test error conversion
        Limitless::ErrorCode convertedError = Limitless::ErrorHandling::ConvertSystemError(0);
        CHECK(convertedError == Limitless::ErrorCode::Success);
    }
    
    TEST_CASE("Try wrapper")
    {
        // Success case
        auto successResult = Limitless::ErrorHandling::Try([]() -> int {
            return 123;
        });
        CHECK(successResult.IsSuccess());
        CHECK(successResult.GetValue() == 123);
        
        // Error case
        auto errorResult = Limitless::ErrorHandling::Try([]() -> int {
            throw Limitless::Error(Limitless::ErrorCode::InvalidArgument, "Test error", std::source_location::current());
        });
        CHECK(errorResult.IsFailure());
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::InvalidArgument);
        
        // Exception case
        auto exceptionResult = Limitless::ErrorHandling::Try([]() -> int {
            throw std::runtime_error("Runtime error");
        });
        CHECK(exceptionResult.IsFailure());
        CHECK(exceptionResult.GetError().GetCode() == Limitless::ErrorCode::Unknown);
    }
    
    TEST_CASE("Assertions and verifications")
    {
        // These should not throw
        Limitless::ErrorHandling::Assert(true, "This should not throw");
        Limitless::ErrorHandling::Verify(true, "This should not throw");
        
        // These should throw
        CHECK_THROWS_AS(Limitless::ErrorHandling::Assert(false, "Assertion failed"), Limitless::Error);
        CHECK_THROWS_AS(Limitless::ErrorHandling::Verify(false, "Verification failed"), Limitless::Error);
    }
    
    TEST_CASE("Error logging")
    {
        // This test just verifies that error logging doesn't crash
        Limitless::Error error(Limitless::ErrorCode::Unknown, "Test error for logging", std::source_location::current());
        CHECK_NOTHROW(Limitless::Error::LogError(error));
    }
}

TEST_SUITE("Integration Tests")
{
    TEST_CASE("Platform and error integration")
    {
        // Initialize platform detection
        Limitless::PlatformDetection::Initialize();
        
        // Create an error with platform context
        Limitless::Error error(Limitless::ErrorCode::PlatformError, "Platform integration test", std::source_location::current());
        
        // Add platform-specific context
        const auto& platformInfo = Limitless::PlatformDetection::GetPlatformInfo();
        error.SetPlatformInfo(platformInfo);
        error.AddContext("Platform", platformInfo.platformName);
        error.AddContext("Architecture", platformInfo.architectureName);
        error.AddContext("Compiler", platformInfo.compilerName);
        
        // Verify context was set
        const auto& context = error.GetContext();
        CHECK_FALSE(context.platformInfo.empty());
        CHECK(error.GetContextValue("Platform") == platformInfo.platformName);
        CHECK(error.GetContextValue("Architecture") == platformInfo.architectureName);
        CHECK(error.GetContextValue("Compiler") == platformInfo.compilerName);
        
        // Test error logging with platform context
        CHECK_NOTHROW(Limitless::Error::LogError(error));
    }
    
    TEST_CASE("System error integration")
    {
        // Test system error integration
        Limitless::SystemError systemError("System error test", std::source_location::current());
        systemError.SetSystemErrorCode(Limitless::ErrorHandling::GetLastSystemError());
        
        CHECK(systemError.GetSystemErrorCode() >= 0);
        
        // Test error conversion
        Limitless::ErrorCode convertedError = Limitless::ErrorHandling::ConvertSystemError(systemError.GetSystemErrorCode());
        CHECK(convertedError != Limitless::ErrorCode::Unknown);
    }
} 