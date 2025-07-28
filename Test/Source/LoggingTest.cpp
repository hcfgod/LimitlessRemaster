#include "doctest.h"
#include "Limitless.h"
#include <thread>
#include <chrono>
#include <iostream>

// Test setup function to ensure LogManager is properly initialized
TEST_SUITE_BEGIN("Logging System Tests");

TEST_CASE("Test Setup")
{
    // Ensure LogManager is initialized for all tests
    auto& logManager = Limitless::LogManager::GetInstance();
    if (!logManager.IsInitialized())
    {
        logManager.Initialize("TestSuite");
        logManager.EnableGlobalFileLogging("logs");
    }
    else
    {
        // Clear any existing loggers to start fresh
        logManager.ClearAllLoggers();
    }
}

TEST_CASE("Test Teardown")
{
    // Clean up after all tests
    auto& logManager = Limitless::LogManager::GetInstance();
    if (logManager.IsInitialized())
    {
        logManager.Shutdown();
    }
}

// Test to verify shutdown access violation is resolved
TEST_CASE("Shutdown Fix Test")
{
    LT_INFO("=== Testing Shutdown Fix ===");
    
    // Test the exact sequence that was causing the access violation
    auto& logManager = Limitless::LogManager::GetInstance();
    
    // Enable file logging
    logManager.EnableGlobalFileLogging("logs");
    
    // Create some loggers
    auto graphicsLogger = logManager.GetLogger("ShutdownTest_Graphics");
    auto audioLogger = logManager.GetLogger("ShutdownTest_Audio");
    
    graphicsLogger->Info("Graphics logger created");
    audioLogger->Info("Audio logger created");
    
    // Test performance monitoring during normal operation
    {
        LT_PERF_SCOPE("Test Performance Scope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Test multiple performance scopes
    {
        LT_PERF_SCOPE("First Scope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    {
        LT_PERF_SCOPE("Second Scope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Test memory logging during normal operation
    LT_LOG_MEMORY("Before shutdown test");
    
    // Test performance function
    LT_PERF_FUNCTION("Test Function", []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    
    // Now test shutdown - this should not cause access violations
    // The key test is that any remaining PerformanceMonitor::Scope objects
    // should not try to access the destroyed logger
    logManager.Shutdown();
    
    // Test that IsInitialized() returns false after shutdown
    CHECK_FALSE(logManager.IsInitialized());
    
    LT_INFO("=== Shutdown Fix Test Complete ===");
}

// Test to verify deadlock issue is resolved
TEST_CASE("Deadlock Fix Test")
{
    LT_INFO("=== Testing Deadlock Fix ===");
    
    // Test the exact sequence that was causing the deadlock
    auto& logManager = Limitless::LogManager::GetInstance();
    
    // Get the default logger - this was causing recursive mutex lock
    auto defaultLogger = logManager.GetDefaultLogger();
    CHECK(defaultLogger != nullptr);
    defaultLogger->Info("Successfully got default logger without deadlock!");
    
    // Test multiple logger access
    auto graphicsLogger = logManager.GetLogger("DeadlockTest_Graphics");
    auto audioLogger = logManager.GetLogger("DeadlockTest_Audio");
    
    CHECK(graphicsLogger != nullptr);
    graphicsLogger->Info("Graphics logger created successfully");
    
    CHECK(audioLogger != nullptr);
    audioLogger->Info("Audio logger created successfully");
    
    // Test global settings
    logManager.SetGlobalLevel(Limitless::LogLevel::Debug);
    logManager.EnableGlobalFileLogging("logs");
    
    // Test that we can still access loggers after global settings
    auto testLogger = logManager.GetLogger("DeadlockTest_Test");
    CHECK(testLogger != nullptr);
    testLogger->Debug("Debug message after global settings");
    
    LT_INFO("=== Deadlock Fix Test Complete ===");
}

TEST_CASE("Logging System Test")
{
    LT_INFO("=== Testing Logging System ===");
    
    // Initialize the logging system
    auto& logManager = Limitless::LogManager::GetInstance();
    logManager.EnableGlobalFileLogging("logs");
    logManager.LogSystemInfo();
    
    // Test basic logging levels
    LT_DEBUG("This is a debug message");
    LT_INFO("This is an info message");
    LT_WARN("This is a warning message");
    LT_ERROR("This is an error message");
    LT_CRITICAL("This is a critical message");
    
    // Test conditional logging
    bool debugMode = true;
    LT_DEBUG_IF(debugMode, "Debug mode is enabled");
    LT_DEBUG_IF(!debugMode, "This should not appear");
    
    // Test performance monitoring
    {
        LT_PERF_SCOPE("Test Performance Scope");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Test function timing
    auto result = LT_PERF_FUNCTION("Test Function", []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        return 42;
    });
    
    LT_INFO("Function returned: {}", result);
    
    // Test memory logging
    LT_LOG_MEMORY("After test operations");
    
    // Test multiple loggers
    auto graphicsLogger = Limitless::GetLogger("LoggingTest_Graphics");
    auto audioLogger = Limitless::GetLogger("LoggingTest_Audio");
    
    CHECK(graphicsLogger != nullptr);
    CHECK(audioLogger != nullptr);
    
    graphicsLogger->Info("Graphics system test");
    audioLogger->Info("Audio system test");
    
    // Test error handling
    try
    {
        LT_THROW_ERROR(Limitless::ErrorCode::InvalidArgument, "Test error message");
    }
    catch (const Limitless::Error& error)
    {
        LT_ERROR("Caught expected error: {}", error.ToString());
    }
    
    // Test Result class
    auto successResult = Limitless::Result<int>(42);
    auto errorResult = Limitless::Result<int>(Limitless::ErrorCode::FileNotFound, "File not found");
    
    CHECK(successResult.IsSuccess());
    CHECK(errorResult.IsFailure());
    
    if (successResult.IsSuccess())
    {
        LT_INFO("Success value: {}", successResult.GetValue());
        CHECK(successResult.GetValue() == 42);
    }
    
    if (errorResult.IsFailure())
    {
        LT_ERROR("Error: {}", errorResult.GetError().GetMessage());
    }
    
    // Test assertions
    bool condition = true;
    LT_ASSERT(condition, "This assertion should pass");
    
    try
    {
        LT_ASSERT(!condition, "This assertion should fail");
    }
    catch (const Limitless::Error& error)
    {
        LT_INFO("Caught assertion error as expected");
    }
    
    LT_INFO("=== Logging System Test Complete ===");
}

TEST_SUITE_END();