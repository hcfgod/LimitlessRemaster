#define DOCTEST_CONFIG_DISABLE_EXCEPTIONS
#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Core/Error.h"
#include <string>
#include <vector>
#include <memory>

TEST_SUITE("Error System")
{
    TEST_CASE("Basic Error Creation")
    {
        // Test basic error creation
        Limitless::Error error(Limitless::ErrorCode::FileNotFound, "File not found: config.json", std::source_location::current());
        
        CHECK(error.GetCode() == Limitless::ErrorCode::FileNotFound);
        CHECK(error.GetErrorMessage() == "File not found: config.json");
        CHECK(error.IsFailure() == true);
        CHECK(error.IsSuccess() == false);
        
        // Test success case
        Limitless::Error success(Limitless::ErrorCode::Success, "Operation completed successfully", std::source_location::current());
        
        CHECK(success.GetCode() == Limitless::ErrorCode::Success);
        CHECK(success.GetErrorMessage() == "Operation completed successfully");
        CHECK(success.IsFailure() == false);
        CHECK(success.IsSuccess() == true);
    }
    
    TEST_CASE("Error Code Enumeration")
    {
        // Test various error codes
        std::vector<Limitless::ErrorCode> errorCodes = {
            Limitless::ErrorCode::Success,
            Limitless::ErrorCode::FileNotFound,
            Limitless::ErrorCode::FileAccessDenied,
            Limitless::ErrorCode::InvalidArgument,
            Limitless::ErrorCode::OutOfMemory,
            Limitless::ErrorCode::NetworkError,
            Limitless::ErrorCode::Timeout,
            Limitless::ErrorCode::NotSupported,
            Limitless::ErrorCode::SystemError
        };
        
        for (auto code : errorCodes)
        {
            Limitless::Error error(code, "Test error", std::source_location::current());
            CHECK(error.GetCode() == code);
        }
    }
    
    TEST_CASE("Error Message Handling")
    {
        // Test empty message
        Limitless::Error emptyMsg(Limitless::ErrorCode::InvalidArgument, "", std::source_location::current());
        CHECK(emptyMsg.GetErrorMessage() == "");
        
        // Test long message
        std::string longMessage = "This is a very long error message that contains multiple sentences. "
                                 "It should be handled properly by the error system without any issues. "
                                 "The message should be stored and retrieved correctly.";
        Limitless::Error longMsg(Limitless::ErrorCode::SystemError, longMessage, std::source_location::current());
        CHECK(longMsg.GetErrorMessage() == longMessage);
        
        // Test special characters
        std::string specialChars = "Error with special chars: !@#$%^&*()_+-=[]{}|;':\",./<>?";
        Limitless::Error specialMsg(Limitless::ErrorCode::InvalidArgument, specialChars, std::source_location::current());
        CHECK(specialMsg.GetErrorMessage() == specialChars);
    }
    
    TEST_CASE("Error Comparison")
    {
        Limitless::Error error1(Limitless::ErrorCode::FileNotFound, "File not found", std::source_location::current());
        Limitless::Error error2(Limitless::ErrorCode::FileNotFound, "File not found", std::source_location::current());
        Limitless::Error error3(Limitless::ErrorCode::FileAccessDenied, "Permission denied", std::source_location::current());
        Limitless::Error success(Limitless::ErrorCode::Success, "Success", std::source_location::current());
        
        // Test code comparison
        CHECK(error1.GetCode() == error2.GetCode());
        CHECK(error1.GetCode() != error3.GetCode());
        CHECK(error1.GetCode() != success.GetCode());
        
        // Test message comparison
        CHECK(error1.GetErrorMessage() == error2.GetErrorMessage());
        CHECK(error1.GetErrorMessage() != error3.GetErrorMessage());
    }
    
    TEST_CASE("Error Copy and Assignment")
    {
        Limitless::Error original(Limitless::ErrorCode::OutOfMemory, "Out of memory", std::source_location::current());
        
        // Test copy constructor
        Limitless::Error copied(original);
        CHECK(copied.GetCode() == original.GetCode());
        CHECK(copied.GetErrorMessage() == original.GetErrorMessage());
        
        // Test assignment operator
        Limitless::Error assigned(Limitless::ErrorCode::Success, "Success", std::source_location::current());
        assigned = original;
        CHECK(assigned.GetCode() == original.GetCode());
        CHECK(assigned.GetErrorMessage() == original.GetErrorMessage());
    }
    
    TEST_CASE("Error Move Semantics")
    {
        Limitless::Error original(Limitless::ErrorCode::NetworkError, "Network connection failed", std::source_location::current());
        
        // Test move constructor
        Limitless::Error moved(std::move(original));
        CHECK(moved.GetCode() == Limitless::ErrorCode::NetworkError);
        CHECK(moved.GetErrorMessage() == "Network connection failed");
        
        // Test move assignment
        Limitless::Error moveAssigned(Limitless::ErrorCode::Success, "Success", std::source_location::current());
        moveAssigned = std::move(moved);
        CHECK(moveAssigned.GetCode() == Limitless::ErrorCode::NetworkError);
        CHECK(moveAssigned.GetErrorMessage() == "Network connection failed");
    }
    
    TEST_CASE("Error Context and Stack Trace")
    {
        Limitless::Error error(Limitless::ErrorCode::InvalidArgument, "Invalid argument provided", std::source_location::current());
        
        // Test setting function name directly
        error.SetFunctionName("ProcessConfig");
        error.SetClassName("ConfigManager");
        error.SetModuleName("Core");
        
        // Test adding additional context
        error.AddContext("Line", "42");
        error.AddContext("File", "ConfigManager.cpp");
        
        // Test context retrieval
        auto context = error.GetContext();
        CHECK(context.functionName == "ProcessConfig");
        CHECK(context.className == "ConfigManager");
        CHECK(context.moduleName == "Core");
        CHECK(context.additionalData["Line"] == "42");
        CHECK(context.additionalData["File"] == "ConfigManager.cpp");
    }
    
    TEST_CASE("Error Severity Levels")
    {
        // Test different severity levels
        Limitless::Error info(Limitless::ErrorCode::Success, "Information message", std::source_location::current(), Limitless::ErrorSeverity::Info);
        CHECK(info.GetSeverity() == Limitless::ErrorSeverity::Info);
        
        Limitless::Error warning(Limitless::ErrorCode::Timeout, "Warning message", std::source_location::current(), Limitless::ErrorSeverity::Warning);
        CHECK(warning.GetSeverity() == Limitless::ErrorSeverity::Warning);
        
        Limitless::Error error(Limitless::ErrorCode::SystemError, "Error message", std::source_location::current(), Limitless::ErrorSeverity::Error);
        CHECK(error.GetSeverity() == Limitless::ErrorSeverity::Error);
        
        Limitless::Error critical(Limitless::ErrorCode::OutOfMemory, "Critical message", std::source_location::current(), Limitless::ErrorSeverity::Critical);
        CHECK(critical.GetSeverity() == Limitless::ErrorSeverity::Critical);
    }
    
    TEST_CASE("Error Serialization")
    {
        Limitless::Error error(Limitless::ErrorCode::NetworkError, "Connection timeout", std::source_location::current());
        error.AddContext("Function", "Connect");
        error.AddContext("Line", "156");
        
        // Test to string conversion
        std::string serialized = error.ToString();
        CHECK(serialized.find("NetworkError") != std::string::npos);
        CHECK(serialized.find("Connection timeout") != std::string::npos);
    }
    
    TEST_CASE("Error Result Pattern")
    {
        // Test successful result
        Limitless::Result<int> successResult(42);
        CHECK(successResult.IsSuccess() == true);
        CHECK(successResult.IsFailure() == false);
        CHECK(successResult.GetValue() == 42);
        
        // Test error result
        Limitless::Result<int> errorResult(Limitless::ErrorCode::InvalidArgument, "Invalid input");
        CHECK(errorResult.IsSuccess() == false);
        CHECK(errorResult.IsFailure() == true);
        CHECK(errorResult.GetError().GetCode() == Limitless::ErrorCode::InvalidArgument);
        CHECK(errorResult.GetError().GetErrorMessage() == "Invalid input");
    }
    
    TEST_CASE("Error Result with Different Types")
    {
        // Test with string
        Limitless::Result<std::string> stringResult("Hello World");
        CHECK(stringResult.IsSuccess() == true);
        CHECK(stringResult.GetValue() == "Hello World");
        
        // Test with vector
        std::vector<int> numbers = {1, 2, 3, 4, 5};
        Limitless::Result<std::vector<int>> vectorResult(numbers);
        CHECK(vectorResult.IsSuccess() == true);
        CHECK(vectorResult.GetValue().size() == 5);
        CHECK(vectorResult.GetValue()[0] == 1);
        CHECK(vectorResult.GetValue()[4] == 5);
        
        // Test with custom type
        struct TestStruct {
            int value;
            std::string name;
        };
        
        TestStruct testObj{42, "test"};
        Limitless::Result<TestStruct> structResult(testObj);
        CHECK(structResult.IsSuccess() == true);
        CHECK(structResult.GetValue().value == 42);
        CHECK(structResult.GetValue().name == "test");
    }
    
    TEST_CASE("Error Propagation")
    {
        // Test error propagation through functions
        auto createError = []() -> Limitless::Result<int> {
            return Limitless::Result<int>(Limitless::ErrorCode::FileNotFound, "File not found");
        };
        
        auto propagateError = [&createError]() -> Limitless::Result<int> {
            auto result = createError();
            if (result.IsFailure()) {
                return result; // Propagate the error
            }
            return Limitless::Result<int>(result.GetValue() * 2);
        };
        
        auto result = propagateError();
        CHECK(result.IsFailure() == true);
        CHECK(result.GetError().GetCode() == Limitless::ErrorCode::FileNotFound);
        CHECK(result.GetError().GetErrorMessage() == "File not found");
    }
    
    TEST_CASE("Error Statistics")
    {
        // Test error statistics tracking
        Limitless::Error error1(Limitless::ErrorCode::FileNotFound, "File 1 not found", std::source_location::current());
        Limitless::Error error2(Limitless::ErrorCode::FileNotFound, "File 2 not found", std::source_location::current());
        Limitless::Error error3(Limitless::ErrorCode::FileAccessDenied, "Permission denied", std::source_location::current());
        
        // Simulate error tracking
        std::vector<Limitless::Error> errors = {error1, error2, error3};
        
        // Count errors by code
        int fileNotFoundCount = 0;
        int fileAccessDeniedCount = 0;
        
        for (const auto& error : errors) {
            if (error.GetCode() == Limitless::ErrorCode::FileNotFound) {
                fileNotFoundCount++;
            } else if (error.GetCode() == Limitless::ErrorCode::FileAccessDenied) {
                fileAccessDeniedCount++;
            }
        }
        
        CHECK(fileNotFoundCount == 2);
        CHECK(fileAccessDeniedCount == 1);
    }
    
    TEST_CASE("Error Edge Cases")
    {
        // Test with system error code
        Limitless::Error systemError(Limitless::ErrorCode::SystemError, "System error", std::source_location::current());
        CHECK(systemError.GetCode() == Limitless::ErrorCode::SystemError);
        
        // Test with very long message
        std::string veryLongMessage(10000, 'x');
        Limitless::Error longError(Limitless::ErrorCode::InvalidArgument, veryLongMessage, std::source_location::current());
        CHECK(longError.GetErrorMessage() == veryLongMessage);
        
        // Test with empty error code (should default to Success)
        Limitless::Error defaultError(Limitless::ErrorCode::Success, "Default error", std::source_location::current());
        CHECK(defaultError.GetCode() == Limitless::ErrorCode::Success);
        
        // Test error with no context
        Limitless::Error noContext(Limitless::ErrorCode::Timeout, "No context", std::source_location::current());
        auto context = noContext.GetContext();
        CHECK(context.functionName.empty() == true);
    }
} 