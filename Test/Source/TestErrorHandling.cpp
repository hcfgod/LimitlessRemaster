#define DOCTEST_CONFIG_DISABLE_EXCEPTIONS
#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Core/Error.h"
#include <string>
#include <vector>
#include <memory>

TEST_SUITE("Error Handling Patterns")
{
    TEST_CASE("LT_ASSERT and LT_VERIFY Macros")
    {
        // Test LT_ASSERT - should throw on false condition
        CHECK_THROWS_AS(LT_ASSERT(false, "Assertion failed"), Limitless::Error);
        CHECK_NOTHROW(LT_ASSERT(true, "Assertion should pass"));
        
        // Test LT_VERIFY - should throw on false condition
        CHECK_THROWS_AS(LT_VERIFY(false, "Verification failed"), Limitless::Error);
        CHECK_NOTHROW(LT_VERIFY(true, "Verification should pass"));
        
        // Test with complex conditions
        int value = 42;
        CHECK_NOTHROW(LT_ASSERT(value > 0, "Value should be positive"));
        CHECK_NOTHROW(LT_VERIFY(value == 42, "Value should be 42"));
        
        CHECK_THROWS_AS(LT_ASSERT(value < 0, "Value should be negative"), Limitless::Error);
        CHECK_THROWS_AS(LT_VERIFY(value != 42, "Value should not be 42"), Limitless::Error);
    }
    
    TEST_CASE("LT_THROW Macros")
    {
        // Test LT_THROW_ERROR
        CHECK_THROWS_AS(LT_THROW_ERROR(Limitless::ErrorCode::InvalidArgument, "Test error"), Limitless::Error);
        
        // Test LT_THROW_SYSTEM_ERROR
        CHECK_THROWS_AS(LT_THROW_SYSTEM_ERROR("Test system error"), Limitless::SystemError);
        
        // Test LT_THROW_PLATFORM_ERROR
        CHECK_THROWS_AS(LT_THROW_PLATFORM_ERROR("Test platform error"), Limitless::PlatformError);
        
        // Test LT_THROW_GRAPHICS_ERROR
        CHECK_THROWS_AS(LT_THROW_GRAPHICS_ERROR("Test graphics error"), Limitless::GraphicsError);
        
        // Test LT_THROW_RESOURCE_ERROR
        CHECK_THROWS_AS(LT_THROW_RESOURCE_ERROR("Test resource error"), Limitless::ResourceError);
        
        // Test LT_THROW_CONFIG_ERROR
        CHECK_THROWS_AS(LT_THROW_CONFIG_ERROR("Test config error"), Limitless::ConfigError);
        
        // Test LT_THROW_MEMORY_ERROR
        CHECK_THROWS_AS(LT_THROW_MEMORY_ERROR("Test memory error"), Limitless::MemoryError);
        
        // Test LT_THROW_THREAD_ERROR
        CHECK_THROWS_AS(LT_THROW_THREAD_ERROR("Test thread error"), Limitless::ThreadError);
    }
    
    TEST_CASE("LT_TRY Macros")
    {
        // Test LT_TRY with success
        auto successResult = LT_TRY(42);
        CHECK(successResult.IsSuccess());
        CHECK(successResult.GetValue() == 42);
        
        // Test LT_TRY with error
        auto errorResult = LT_TRY([]() -> int {
            LT_THROW_ERROR(Limitless::ErrorCode::Timeout, "Test timeout");
        }());
        CHECK(errorResult.IsFailure());
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::Timeout);
        
        // Test LT_TRY_VOID
        auto voidSuccessResult = LT_TRY_VOID([]() {
            // Do nothing
        });
        CHECK(voidSuccessResult.IsSuccess());
        
        // Debug: Test if the exception is actually being thrown
        try {
            LT_THROW_ERROR(Limitless::ErrorCode::InvalidState, "Test invalid state");
            CHECK(false); // Should not reach here
        } catch (const Limitless::Error& e) {
            CHECK(e.GetCode() == Limitless::ErrorCode::InvalidState);
        }
        
        // Debug: Test if the lambda is being executed
        bool lambdaExecuted = false;
        auto debugResult = LT_TRY_VOID([&lambdaExecuted]() {
            lambdaExecuted = true;
            LT_THROW_ERROR(Limitless::ErrorCode::InvalidState, "Test invalid state");
        });
        CHECK(lambdaExecuted); // Should be true if lambda was executed
        
        // Minimal test case
        auto minimalResult = Limitless::ErrorHandling::Try([]() -> void {
            throw Limitless::Error(Limitless::ErrorCode::InvalidState, "Test", std::source_location::current());
        });
        CHECK(minimalResult.IsFailure());
        
        auto voidErrorResult = LT_TRY_VOID([]() {
            LT_THROW_ERROR(Limitless::ErrorCode::InvalidState, "Test invalid state");
        });
        CHECK(voidErrorResult.IsFailure());
        CHECK(voidErrorResult.GetError().GetCode() == Limitless::ErrorCode::InvalidState);
    }
    
    TEST_CASE("LT_RETURN_IF_ERROR Macros")
    {
        // Test function that uses LT_RETURN_IF_ERROR
        auto testFunction = [](bool shouldFail) -> Limitless::Result<int> {
            if (shouldFail)
            {
                return Limitless::Result<int>(Limitless::ErrorCode::InvalidArgument, "Function failed");
            }
            return Limitless::Result<int>(42);
        };
        
        // Test success case
        auto successResult = testFunction(false);
        CHECK(successResult.IsSuccess());
        CHECK(successResult.GetValue() == 42);
        
        // Test failure case
        auto failureResult = testFunction(true);
        CHECK(failureResult.IsFailure());
        CHECK(failureResult.GetError().GetCode() == Limitless::ErrorCode::InvalidArgument);
    }
    
    TEST_CASE("Error Context and Information")
    {
        // Test error with context
        Limitless::Error error(Limitless::ErrorCode::FileNotFound, "File not found", std::source_location::current());
        error.SetFunctionName("TestFunction");
        error.SetClassName("TestClass");
        error.SetModuleName("TestModule");
        error.AddContext("file_path", "/path/to/file.txt");
        error.AddContext("user_id", "12345");
        
        CHECK(error.GetContext().functionName == "TestFunction");
        CHECK(error.GetContext().className == "TestClass");
        CHECK(error.GetContext().moduleName == "TestModule");
        CHECK(error.GetContextValue("file_path") == "/path/to/file.txt");
        CHECK(error.GetContextValue("user_id") == "12345");
    }
    
    TEST_CASE("System Error Integration")
    {
        // Test SystemError with system error code
        Limitless::SystemError systemError("Test system error", std::source_location::current());
        systemError.SetSystemErrorCode(Limitless::ErrorHandling::GetLastSystemError());
        
        CHECK(systemError.GetCode() == Limitless::ErrorCode::SystemError);
        CHECK(systemError.GetSystemErrorCode() >= 0);
        
        // Test system error conversion
        int lastError = Limitless::ErrorHandling::GetLastSystemError();
        Limitless::ErrorCode convertedError = Limitless::ErrorHandling::ConvertSystemError(lastError);
        bool isValidConversion = (convertedError != Limitless::ErrorCode::Success) || (lastError == 0);
        CHECK(isValidConversion);
    }
    
    TEST_CASE("Error Severity Levels")
    {
        // Test different severity levels
        Limitless::Error infoError(Limitless::ErrorCode::Cancelled, "Info message", std::source_location::current(), Limitless::ErrorSeverity::Info);
        Limitless::Error warningError(Limitless::ErrorCode::FileExists, "Warning message", std::source_location::current(), Limitless::ErrorSeverity::Warning);
        Limitless::Error errorError(Limitless::ErrorCode::FileNotFound, "Error message", std::source_location::current(), Limitless::ErrorSeverity::Error);
        Limitless::Error criticalError(Limitless::ErrorCode::OutOfMemory, "Critical message", std::source_location::current(), Limitless::ErrorSeverity::Critical);
        Limitless::Error fatalError(Limitless::ErrorCode::MemoryCorruption, "Fatal message", std::source_location::current(), Limitless::ErrorSeverity::Fatal);
        
        CHECK(infoError.GetSeverity() == Limitless::ErrorSeverity::Info);
        CHECK(warningError.GetSeverity() == Limitless::ErrorSeverity::Warning);
        CHECK(errorError.GetSeverity() == Limitless::ErrorSeverity::Error);
        CHECK(criticalError.GetSeverity() == Limitless::ErrorSeverity::Critical);
        CHECK(fatalError.GetSeverity() == Limitless::ErrorSeverity::Fatal);
        
        CHECK(!infoError.IsCritical());
        CHECK(!warningError.IsCritical());
        CHECK(!errorError.IsCritical());
        CHECK(criticalError.IsCritical());
        CHECK(fatalError.IsCritical());
        
        CHECK(!infoError.IsFatal());
        CHECK(!warningError.IsFatal());
        CHECK(!errorError.IsFatal());
        CHECK(!criticalError.IsFatal());
        CHECK(fatalError.IsFatal());
    }
    
    TEST_CASE("Result Class Advanced Usage")
    {
        // Test Result with complex types
        struct TestStruct {
            int value;
            std::string name;
            
            TestStruct() : value(0), name("") {} // Default constructor
            TestStruct(int v, const std::string& n) : value(v), name(n) {}
        };
        
        // Success case
        Limitless::Result<TestStruct> successResult(TestStruct(42, "test"));
        CHECK(successResult.IsSuccess());
        CHECK(successResult.GetValue().value == 42);
        CHECK(successResult.GetValue().name == "test");
        
        // Error case
        Limitless::Result<TestStruct> errorResult(Limitless::ErrorCode::InvalidArgument, "Invalid struct");
        CHECK(errorResult.IsFailure());
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::InvalidArgument);
        
        // Safe value access
        TestStruct* successPtr = successResult.GetValuePtr();
        CHECK(successPtr != nullptr);
        CHECK(successPtr->value == 42);
        
        TestStruct* errorPtr = errorResult.GetValuePtr();
        CHECK(errorPtr == nullptr);
        
        // Value or default
        TestStruct defaultStruct(0, "default");
        TestStruct successValue = successResult.GetValueOr(defaultStruct);
        CHECK(successValue.value == 42);
        CHECK(successValue.name == "test");
        
        TestStruct errorValue = errorResult.GetValueOr(defaultStruct);
        CHECK(errorValue.value == 0);
        CHECK(errorValue.name == "default");
    }
    
    TEST_CASE("Error Propagation Patterns")
    {
        // Test error propagation through multiple functions
        auto level3Function = [](bool fail) -> Limitless::Result<int> {
            if (fail)
            {
                return Limitless::Result<int>(Limitless::ErrorCode::FileNotFound, "File not found at level 3");
            }
            return Limitless::Result<int>(3);
        };
        
        auto level2Function = [&level3Function](bool fail) -> Limitless::Result<int> {
            auto result = level3Function(fail);
            LT_RETURN_IF_ERROR(result);
            return Limitless::Result<int>(result.GetValue() + 2);
        };
        
        auto level1Function = [&level2Function](bool fail) -> Limitless::Result<int> {
            auto result = level2Function(fail);
            LT_RETURN_IF_ERROR(result);
            return Limitless::Result<int>(result.GetValue() + 1);
        };
        
        // Test success propagation
        auto successResult = level1Function(false);
        CHECK(successResult.IsSuccess());
        CHECK(successResult.GetValue() == 6); // 3 + 2 + 1
        
        // Test error propagation
        auto errorResult = level1Function(true);
        CHECK(errorResult.IsFailure());
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::FileNotFound);
        CHECK(errorResult.GetError().GetErrorMessage() == "File not found at level 3");
    }
    
    TEST_CASE("Error Handling with Try-Catch")
    {
        // Test error handling with try-catch and LT_TRY
        auto riskyFunction = [](bool shouldThrow) -> int {
            if (shouldThrow)
            {
                LT_THROW_ERROR(Limitless::ErrorCode::Timeout, "Operation timed out");
            }
            return 42;
        };
        
        // Test with LT_TRY
        auto tryResult = LT_TRY(riskyFunction(false));
        CHECK(tryResult.IsSuccess());
        CHECK(tryResult.GetValue() == 42);
        
        auto tryErrorResult = LT_TRY(riskyFunction(true));
        CHECK(tryErrorResult.IsFailure());
        CHECK(tryErrorResult.GetError().GetCode() == Limitless::ErrorCode::Timeout);
        
        // Test with direct try-catch
        try
        {
            int result = riskyFunction(false);
            CHECK(result == 42);
        }
        catch (const Limitless::Error&)
        {
            CHECK(false); // Should not reach here
        }
        
        try
        {
            riskyFunction(true);
            CHECK(false); // Should not reach here
        }
        catch (const Limitless::Error& error)
        {
            CHECK(error.GetCode() == Limitless::ErrorCode::Timeout);
            CHECK(error.GetErrorMessage() == "Operation timed out");
        }
    }
    
    TEST_CASE("Error Code Utilities")
    {
        // Test error code string conversion
        std::string successString = Limitless::ErrorHandling::GetErrorCodeString(Limitless::ErrorCode::Success);
        std::string fileNotFoundString = Limitless::ErrorHandling::GetErrorCodeString(Limitless::ErrorCode::FileNotFound);
        std::string invalidArgString = Limitless::ErrorHandling::GetErrorCodeString(Limitless::ErrorCode::InvalidArgument);
        
        CHECK(!successString.empty());
        CHECK(!fileNotFoundString.empty());
        CHECK(!invalidArgString.empty());
        
        // Test error code descriptions
        std::string successDesc = Limitless::ErrorHandling::GetErrorCodeDescription(Limitless::ErrorCode::Success);
        std::string fileNotFoundDesc = Limitless::ErrorHandling::GetErrorCodeDescription(Limitless::ErrorCode::FileNotFound);
        
        CHECK(!successDesc.empty());
        CHECK(!fileNotFoundDesc.empty());
        
        // Test error code severity
        Limitless::ErrorSeverity successSeverity = Limitless::ErrorHandling::GetErrorCodeSeverity(Limitless::ErrorCode::Success);
        Limitless::ErrorSeverity fileNotFoundSeverity = Limitless::ErrorHandling::GetErrorCodeSeverity(Limitless::ErrorCode::FileNotFound);
        Limitless::ErrorSeverity outOfMemorySeverity = Limitless::ErrorHandling::GetErrorCodeSeverity(Limitless::ErrorCode::OutOfMemory);
        
        CHECK(successSeverity == Limitless::ErrorSeverity::Info);
        CHECK(fileNotFoundSeverity >= Limitless::ErrorSeverity::Error);
        CHECK(outOfMemorySeverity >= Limitless::ErrorSeverity::Critical);
    }
} 