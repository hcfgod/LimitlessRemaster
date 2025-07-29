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