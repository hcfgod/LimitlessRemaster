# Logging System Integration Guide

## Overview

The Limitless Engine logging system is now fully integrated with the ConfigManager, allowing you to configure logging behavior through the `config.json` file or command-line arguments.

## Configuration Options

The logging system supports the following configuration options in the `config.json` file:

```json
{
  "logging": {
    "level": "info",
    "file_enabled": true,
    "console_enabled": true,
    "pattern": "[%T] [%l] %n: %v",
    "directory": "logs",
    "max_file_size": 52428800,
    "max_files": 10
  }
}
```

### Configuration Parameters

- **`level`**: Log level (`trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`)
- **`file_enabled`**: Enable/disable file logging
- **`console_enabled`**: Enable/disable console logging
- **`pattern`**: Log message format pattern
- **`directory`**: Directory where log files are stored
- **`max_file_size`**: Maximum size of each log file in bytes (default: 50MB)
- **`max_files`**: Maximum number of backup log files to keep

## Usage

### Basic Usage

The logging system is automatically initialized in the `EntryPoint.h` with configuration from `config.json`:

```cpp
// The system automatically loads configuration and initializes logging
auto& configManager = Limitless::ConfigManager::GetInstance();
configManager.Initialize("config.json");

// Logging is initialized with config settings
Limitless::Log::Init(logLevel, fileEnabled, consoleEnabled, pattern, directory, maxFileSize, maxFiles);
```

### Logging Macros

Use the provided macros for logging:

```cpp
// Core engine logging (for engine internals)
LT_CORE_TRACE("Trace message");
LT_CORE_DEBUG("Debug message");
LT_CORE_INFO("Info message");
LT_CORE_WARN("Warning message");
LT_CORE_ERROR("Error message");
LT_CORE_CRITICAL("Critical message");

// Client application logging (for your application)
LT_TRACE("Trace message");
LT_DEBUG("Debug message");
LT_INFO("Info message");
LT_WARN("Warning message");
LT_ERROR("Error message");
LT_CRITICAL("Critical message");
```

### Configuration Access

You can access logging configuration values in your code:

```cpp
auto& config = Limitless::ConfigManager::GetInstance();

// Get logging level
std::string level = config.GetValue<std::string>(Limitless::Config::Logging::LEVEL, "info");

// Check if file logging is enabled
bool fileEnabled = config.GetValue<bool>(Limitless::Config::Logging::FILE_ENABLED, true);

// Get log directory
std::string logDir = config.GetValue<std::string>(Limitless::Config::Logging::DIRECTORY, "logs");
```

## Command Line Configuration

You can override configuration values via command line:

```bash
# Set log level to debug
./YourApp --logging.level=debug

# Disable file logging
./YourApp --logging.file_enabled=false

# Enable console logging only
./YourApp --logging.console_enabled=true --logging.file_enabled=false

# Set custom log directory
./YourApp --logging.directory=custom_logs
```

## Log Patterns

The log pattern uses spdlog format specifiers:

- `%T`: Time in HH:MM:SS format
- `%l`: Log level
- `%n`: Logger name
- `%v`: Log message
- `%^` and `%$`: Color markers (for console output)

### Example Patterns

```json
{
  "logging": {
    "pattern": "[%T] [%l] %n: %v"  // [14:30:25] [info] APP: Application started
  }
}
```

## File Rotation

Log files are automatically rotated when they reach the maximum size:

- Files are named: `Limitless.log`, `Limitless.log.1`, `Limitless.log.2`, etc.
- Old files are automatically deleted when the maximum number is reached
- Default: 50MB per file, 10 backup files (500MB total)

## Integration Example

Here's a complete example showing how the logging system integrates with your application:

```cpp
#include "Limitless.h"

class MyApp : public Limitless::Application {
public:
    bool Initialize() override {
        LT_INFO("MyApp initialized successfully!");
        
        // Log configuration values
        auto& config = Limitless::ConfigManager::GetInstance();
        LT_INFO("Window size: {}x{}", 
                config.GetValue<int>(Limitless::Config::Window::WIDTH, 1280),
                config.GetValue<int>(Limitless::Config::Window::HEIGHT, 720));
        
        return true;
    }
    
    void Shutdown() override {
        LT_INFO("MyApp shutting down...");
    }
};

Limitless::Application* CreateApplication() {
    return new MyApp();
}
```

## Best Practices

1. **Use appropriate log levels**: Use `TRACE` for detailed debugging, `INFO` for general information, `WARN` for warnings, and `ERROR`/`CRITICAL` for errors.

2. **Configure for production**: In production builds, consider setting the log level to `warn` or `error` to reduce log file size.

3. **Monitor log files**: Regularly check log file sizes and clean up old logs if needed.

4. **Use structured logging**: Include relevant context in your log messages for better debugging.

5. **Test configuration**: Verify your logging configuration works as expected in different environments.

## Troubleshooting

### Common Issues

1. **Logs not appearing**: Check if the log level is set too high or if both file and console logging are disabled.

2. **Permission errors**: Ensure the application has write permissions to the log directory.

3. **Large log files**: Adjust `max_file_size` and `max_files` settings to control log file growth.

4. **Performance impact**: In release builds, consider disabling file logging or using a higher log level.

### Debug Configuration

To debug logging configuration issues, you can temporarily force console output:

```json
{
  "logging": {
    "level": "trace",
    "file_enabled": false,
    "console_enabled": true
  }
}
```

This will show all log messages in the console, making it easier to diagnose issues.