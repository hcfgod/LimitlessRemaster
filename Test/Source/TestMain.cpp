#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Limitless.h"
#include "Core/HotReloadManager.h"

TEST_CASE("Logging Configuration Integration") {
    // Test that the logging system can be initialized with configuration
    auto& config = Limitless::ConfigManager::GetInstance();
    
    // Set some test configuration values
    config.SetValue(Limitless::Config::Logging::LEVEL, std::string("debug"));
    config.SetValue(Limitless::Config::Logging::FILE_ENABLED, true);
    config.SetValue(Limitless::Config::Logging::CONSOLE_ENABLED, true);
    
    // Verify the values are set correctly
    CHECK(config.GetValue<std::string>(Limitless::Config::Logging::LEVEL) == "debug");
    CHECK(config.GetValue<bool>(Limitless::Config::Logging::FILE_ENABLED) == true);
    CHECK(config.GetValue<bool>(Limitless::Config::Logging::CONSOLE_ENABLED) == true);
    
    // Test that default values work when keys don't exist
    CHECK(config.GetValue<std::string>(Limitless::Config::Logging::PATTERN, "default") == "default");
    CHECK(config.GetValue<size_t>(Limitless::Config::Logging::MAX_FILES, 5) == 5);
}

TEST_CASE("Configuration File Loading") {
    // Test that configuration can be loaded from a JSON file
    auto& config = Limitless::ConfigManager::GetInstance();
    
    // Create a test JSON configuration
    nlohmann::json testConfig;
    testConfig["logging"]["level"] = "trace";
    testConfig["logging"]["file_enabled"] = false;
    testConfig["logging"]["console_enabled"] = true;
    testConfig["logging"]["pattern"] = "TEST: %v";
    testConfig["logging"]["directory"] = "test_logs";
    testConfig["logging"]["max_file_size"] = 1048576; // 1MB
    testConfig["logging"]["max_files"] = 5;
    
    // Load the configuration
    config.FromJson(testConfig);
    
    // Verify the values were loaded correctly
    CHECK(config.GetValue<std::string>(Limitless::Config::Logging::LEVEL) == "trace");
    CHECK(config.GetValue<bool>(Limitless::Config::Logging::FILE_ENABLED) == false);
    CHECK(config.GetValue<bool>(Limitless::Config::Logging::CONSOLE_ENABLED) == true);
    CHECK(config.GetValue<std::string>(Limitless::Config::Logging::PATTERN) == "TEST: %v");
    CHECK(config.GetValue<std::string>(Limitless::Config::Logging::DIRECTORY) == "test_logs");
    CHECK(config.GetValue<size_t>(Limitless::Config::Logging::MAX_FILE_SIZE) == 1048576);
    CHECK(config.GetValue<size_t>(Limitless::Config::Logging::MAX_FILES) == 5);
}

TEST_CASE("Window Configuration Loading") {
    // Test that window configuration can be loaded from a JSON file
    auto& config = Limitless::ConfigManager::GetInstance();
    
    // Create a test JSON configuration
    nlohmann::json testConfig;
    testConfig["window"]["width"] = 1920;
    testConfig["window"]["height"] = 1080;
    testConfig["window"]["title"] = "Test Window";
    testConfig["window"]["fullscreen"] = true;
    testConfig["window"]["resizable"] = false;
    testConfig["window"]["vsync"] = false;
    
    // Load the configuration
    config.FromJson(testConfig);
    
    // Verify the values were loaded correctly
    CHECK(config.GetValue<uint32_t>(Limitless::Config::Window::WIDTH) == 1920);
    CHECK(config.GetValue<uint32_t>(Limitless::Config::Window::HEIGHT) == 1080);
    CHECK(config.GetValue<std::string>(Limitless::Config::Window::TITLE) == "Test Window");
    CHECK(config.GetValue<bool>(Limitless::Config::Window::FULLSCREEN) == true);
    CHECK(config.GetValue<bool>(Limitless::Config::Window::RESIZABLE) == false);
    CHECK(config.GetValue<bool>(Limitless::Config::Window::VSYNC) == false);
}

TEST_CASE("Nested JSON Configuration") {
    // Test that nested JSON configuration can be loaded and saved correctly
    auto& config = Limitless::ConfigManager::GetInstance();
    
    // Create a test JSON configuration with nested structure
    nlohmann::json testConfig;
    testConfig["logging"]["level"] = "trace";
    testConfig["logging"]["file_enabled"] = false;
    testConfig["window"]["width"] = 1920;
    testConfig["window"]["height"] = 1080;
    testConfig["window"]["title"] = "Nested Test";
    testConfig["system"]["max_threads"] = 16;
    
    // Load the configuration
    config.FromJson(testConfig);
    
    // Verify the values were loaded correctly with dot notation
    CHECK(config.GetValue<std::string>("logging.level") == "trace");
    CHECK(config.GetValue<bool>("logging.file_enabled") == false);
    CHECK(config.GetValue<uint32_t>("window.width") == 1920);
    CHECK(config.GetValue<uint32_t>("window.height") == 1080);
    CHECK(config.GetValue<std::string>("window.title") == "Nested Test");
    CHECK(config.GetValue<int>("system.max_threads") == 16);
    
    // Test that saving reconstructs the nested structure
    nlohmann::json savedConfig = config.ToJson();
    
    // Verify the nested structure is preserved
    CHECK(savedConfig["logging"]["level"] == "trace");
    CHECK(savedConfig["logging"]["file_enabled"] == false);
    CHECK(savedConfig["window"]["width"] == 1920);
    CHECK(savedConfig["window"]["height"] == 1080);
    CHECK(savedConfig["window"]["title"] == "Nested Test");
    CHECK(savedConfig["system"]["max_threads"] == 16);
}

TEST_CASE("Hot Reload Functionality") {
    // Test that hot reload functionality can be enabled and disabled
    auto& hotReloadManager = Limitless::HotReloadManager::GetInstance();
    
    // Test initial state
    CHECK(hotReloadManager.IsHotReloadEnabled() == false);
    
    // Test enabling hot reload
    hotReloadManager.EnableHotReload(true);
    CHECK(hotReloadManager.IsHotReloadEnabled() == true);
    
    // Test disabling hot reload
    hotReloadManager.EnableHotReload(false);
    CHECK(hotReloadManager.IsHotReloadEnabled() == false);
    
    // Test configuration change callbacks
    auto& config = Limitless::ConfigManager::GetInstance();
    
    // Set up a test configuration
    config.SetValue("logging.level", std::string("info"));
    config.SetValue("window.width", 1280);
    
    // Verify initial values
    CHECK(config.GetValue<std::string>("logging.level") == "info");
    CHECK(config.GetValue<int>("window.width") == 1280);
    
    // Simulate configuration changes (these would normally be triggered by file changes)
    config.SetValue("logging.level", std::string("debug"));
    config.SetValue("window.width", 1920);
    
    // Verify values were updated
    CHECK(config.GetValue<std::string>("logging.level") == "debug");
    CHECK(config.GetValue<int>("window.width") == 1920);
}

TEST_CASE("Basic Logging Functionality") {
    // Initialize logging system for testing
    Limitless::Log::Init("debug", true, true, "[%H:%M:%S.%e] [%l] %v", "test_logs", 1024*1024, 5);
    
    // Test that logging is initialized
    CHECK(Limitless::Log::IsInitialized() == true);
    
    // Test basic logging macros - these should not crash
    LT_TRACE("This is a trace message");
    LT_DBG("This is a debug message");
    LT_INFO("This is an info message");
    LT_WARN("This is a warning message");
    LT_ERROR("This is an error message");
    LT_CRITICAL("This is a critical message");
    
    // Test logging with formatting
    LT_INFO("Testing formatted message: {} + {} = {}", 1, 2, 3);
    LT_DBG("Testing debug with values: x={}, y={}, result={}", 10.5f, 20.3f, 30.8f);
    
    // Test logging with ConfigValue
    auto& config = Limitless::ConfigManager::GetInstance();
    config.SetValue("test.value", std::string("test_string"));
    config.SetValue("test.number", 42);
    config.SetValue("test.boolean", true);
    
    LT_INFO("Config value: {}", config.GetValue<std::string>("test.value"));
    LT_DBG("Config number: {}", config.GetValue<int>("test.number"));
    LT_INFO("Config boolean: {}", config.GetValue<bool>("test.boolean"));
    
    // Cleanup
    Limitless::Log::Shutdown();
}

TEST_CASE("Conditional Logging Functionality") {
    // Initialize logging system for testing
    Limitless::Log::Init("debug", true, true, "[%H:%M:%S.%e] [%l] %v", "test_logs", 1024*1024, 5);
    
    // Test conditional logging with true condition
    bool condition_true = true;
    bool condition_false = false;
    
    // These should log
    LT_TRACE_IF(condition_true, "This trace should appear");
    LT_DBG_IF(condition_true, "This debug should appear");
    LT_INFO_IF(condition_true, "This info should appear");
    LT_WARN_IF(condition_true, "This warning should appear");
    LT_ERROR_IF(condition_true, "This error should appear");
    LT_CRITICAL_IF(condition_true, "This critical should appear");
    
    // These should NOT log
    LT_TRACE_IF(condition_false, "This trace should NOT appear");
    LT_DBG_IF(condition_false, "This debug should NOT appear");
    LT_INFO_IF(condition_false, "This info should NOT appear");
    LT_WARN_IF(condition_false, "This warning should NOT appear");
    LT_ERROR_IF(condition_false, "This error should NOT appear");
    LT_CRITICAL_IF(condition_false, "This critical should NOT appear");
    
    // Test conditional logging with complex conditions
    int value = 42;
    LT_DBG_IF(value > 40, "Value is greater than 40: {}", value);
    LT_INFO_IF(value == 42, "Value is exactly 42: {}", value);
    LT_WARN_IF(value < 50, "Value is less than 50: {}", value);
    LT_ERROR_IF(value != 42, "This error should NOT appear");
    
    // Test conditional logging with function calls
    auto testFunction = []() { return true; };
    auto testFunctionFalse = []() { return false; };
    
    LT_DBG_IF(testFunction(), "Function returned true");
    LT_INFO_IF(testFunctionFalse(), "This should NOT appear");
    
    // Test conditional logging with multiple conditions
    bool first_condition = true;
    bool second_condition = true;
    bool third_condition = false;
    
    LT_DBG_IF(first_condition && second_condition, "Both conditions are true");
    LT_INFO_IF(first_condition && third_condition, "This should NOT appear");
    LT_WARN_IF(first_condition || third_condition, "At least one condition is true");
    
    // Cleanup
    Limitless::Log::Shutdown();
}

TEST_CASE("Core Logging Functionality") {
    // Initialize logging system for testing
    Limitless::Log::Init("debug", true, true, "[%H:%M:%S.%e] [%l] %v", "test_logs", 1024*1024, 5);
    
    // Test core logging macros - these should not crash
    LT_CORE_TRACE("This is a core trace message");
    LT_CORE_DEBUG("This is a core debug message");
    LT_CORE_INFO("This is a core info message");
    LT_CORE_WARN("This is a core warning message");
    LT_CORE_ERROR("This is a core error message");
    LT_CORE_CRITICAL("This is a core critical message");
    
    // Test conditional core logging
    bool condition = true;
    LT_CORE_TRACE_IF(condition, "This core trace should appear");
    LT_CORE_DEBUG_IF(condition, "This core debug should appear");
    LT_CORE_INFO_IF(condition, "This core info should appear");
    LT_CORE_WARN_IF(condition, "This core warning should appear");
    LT_CORE_ERROR_IF(condition, "This core error should appear");
    LT_CORE_CRITICAL_IF(condition, "This core critical should appear");
    
    // Test conditional core logging with false condition
    LT_CORE_TRACE_IF(false, "This core trace should NOT appear");
    LT_CORE_DEBUG_IF(false, "This core debug should NOT appear");
    LT_CORE_INFO_IF(false, "This core info should NOT appear");
    LT_CORE_WARN_IF(false, "This core warning should NOT appear");
    LT_CORE_ERROR_IF(false, "This core error should NOT appear");
    LT_CORE_CRITICAL_IF(false, "This core critical should NOT appear");
    
    // Cleanup
    Limitless::Log::Shutdown();
}

TEST_CASE("Logging Edge Cases") {
    // Test logging when not initialized
    CHECK(Limitless::Log::IsInitialized() == false);
    
    // These should not crash even when logging is not initialized
    LT_TRACE("Trace without initialization");
    LT_DBG("Debug without initialization");
    LT_INFO("Info without initialization");
    LT_WARN("Warning without initialization");
    LT_ERROR("Error without initialization");
    LT_CRITICAL("Critical without initialization");
    
    // Test conditional logging when not initialized
    LT_TRACE_IF(true, "Trace if without initialization");
    LT_DBG_IF(true, "Debug if without initialization");
    LT_INFO_IF(true, "Info if without initialization");
    LT_WARN_IF(true, "Warning if without initialization");
    LT_ERROR_IF(true, "Error if without initialization");
    LT_CRITICAL_IF(true, "Critical if without initialization");
    
    // Test logging with empty messages
    Limitless::Log::Init("debug", true, true, "[%H:%M:%S.%e] [%l] %v", "test_logs", 1024*1024, 5);
    
    LT_TRACE("");
    LT_DBG("");
    LT_INFO("");
    LT_WARN("");
    LT_ERROR("");
    LT_CRITICAL("");
    
    // Test conditional logging with empty messages
    LT_TRACE_IF(true, "");
    LT_DBG_IF(true, "");
    LT_INFO_IF(true, "");
    LT_WARN_IF(true, "");
    LT_ERROR_IF(true, "");
    LT_CRITICAL_IF(true, "");
    
    // Test logging with special characters
    LT_INFO("Special chars: !@#$%^&*()_+-=[]{}|;':\",./<>?");
    LT_DBG("Unicode test: 你好世界 🌍");
    LT_WARN("Newlines:\nLine 1\nLine 2\nLine 3");
    
    // Cleanup
    Limitless::Log::Shutdown();
}