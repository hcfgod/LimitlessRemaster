# Hot Reload Configuration Guide

## Overview

The Limitless Engine now supports hot reloading of configuration files! This means you can change settings in `config.json` while the application is running and see the changes take effect immediately without restarting.

## How It Works

The hot reload system consists of several components:

1. **FileWatcher**: Monitors the `config.json` file for changes using file system polling
2. **ConfigManager**: Handles configuration loading and change detection
3. **HotReloadManager**: Manages system-specific hot reloading (logging, window, etc.)

## Supported Hot Reload Features

### ✅ **Logging System (Fully Supported)**
- **Log Level**: Change between `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`
- **File Logging**: Enable/disable file output
- **Console Logging**: Enable/disable console output
- **Log Pattern**: Change the log message format
- **Log Directory**: Change where log files are stored
- **File Size/Count**: Adjust rotation settings

### ✅ **Window System (Fully Supported)**
- **Window Title**: Change window title in real-time
- **Window Size**: Change width and height in real-time
- **Window Position**: Change x and y position in real-time
- **Fullscreen**: Toggle fullscreen mode in real-time
- **Resizable**: Enable/disable window resizing in real-time
- **VSync**: Enable/disable vertical synchronization in real-time
- **Borderless**: Toggle borderless mode in real-time
- **Always on Top**: Toggle always-on-top behavior in real-time
- **High DPI**: Enable/disable high DPI support in real-time
- **Window Icon**: Change window icon in real-time
- **Size Constraints**: Change minimum and maximum window sizes in real-time

## Usage

### 1. Start the Application
```bash
./Sandbox.exe
```

You should see output like:
```
FileWatcher: Started watching config.json
ConfigManager: Hot reload enabled for config.json
HotReloadManager: Hot reload enabled
```

### 2. Modify Configuration
While the application is running, edit `config.json`:

```json
{
  "logging": {
    "level": "trace",  // Changed from "debug" to "trace"
    "file_enabled": false,  // Disabled file logging
    "console_enabled": true,
    "pattern": "[%T] [%l] %n: %v"
  },
  "window": {
    "width": 1920,  // Changed from 1024
    "height": 1080, // Changed from 768
    "title": "Hot Reload Test"
  }
}
```

### 3. Watch the Changes
You should see output like:
```
FileWatcher: Detected change in config.json
ConfigManager: Hot reload triggered for config.json
ConfigManager: Reloading configuration from config.json
ConfigManager: Value changed for key 'logging.level'
ConfigManager: Value changed for key 'logging.file_enabled'
ConfigManager: Value changed for key 'window.width'
ConfigManager: Value changed for key 'window.title'
HotReloadManager: Logging configuration changed - logging.level
HotReloadManager: Reinitializing logging system...
Logging configuration:
  Level: trace
  File enabled: false
  Console enabled: true
  Pattern: [%T] [%l] %n: %v
  Directory: logs
  Max file size: 50MB
  Max files: 10
Limitless Engine Logger Initialized!
Application Logger Initialized!
HotReloadManager: Logging system reinitialized
HotReloadManager: Window configuration changed - window.width
HotReloadManager: Window configuration changed - window.title
Window configuration changed: window.width = 1920
Window configuration change applied successfully
Window configuration changed: window.title = Hot Reload Test
Window configuration change applied successfully
```

## Configuration Examples

### Change Log Level in Real-Time
```json
{
  "logging": {
    "level": "trace"  // Shows all log messages
  }
}
```
**Effect**: Immediately shows trace and debug messages that were previously hidden.

### Disable File Logging
```json
{
  "logging": {
    "file_enabled": false
  }
}
```
**Effect**: Stops writing to log files, only console output remains.

### Change Log Pattern
```json
{
  "logging": {
    "pattern": "[%Y-%m-%d %H:%M:%S] [%l] %v"
  }
}
```
**Effect**: Changes the format of log messages to include date.

### Change Window Title
```json
{
  "window": {
    "title": "My New Window Title"
  }
}
```
**Effect**: Immediately changes the window title bar text.

### Resize Window
```json
{
  "window": {
    "width": 1920,
    "height": 1080
  }
}
```
**Effect**: Immediately resizes the window to the new dimensions.

### Toggle Fullscreen
```json
{
  "window": {
    "fullscreen": true
  }
}
```
**Effect**: Immediately switches the window to fullscreen mode.

### Change Window Position
```json
{
  "window": {
    "position": {
      "x": 100,
      "y": 200
    }
  }
}
```
**Effect**: Immediately moves the window to the new position.

### Enable Full Debug Output
```json
{
  "logging": {
    "level": "trace",
    "file_enabled": true,
    "console_enabled": true,
    "pattern": "[%T] [%l] %n: %v"
  }
}
```
**Effect**: Maximum debugging information with both file and console output.

## Technical Details

### File Watching
- **Polling Interval**: 500ms (configurable)
- **File Detection**: Uses file system last write time
- **Error Handling**: Graceful handling of file access errors

### Change Detection
- **Value Comparison**: Detects actual value changes, not just file modifications
- **Callback System**: Notifies specific systems when their configuration changes
- **Thread Safety**: All operations are thread-safe

### Performance
- **Low Overhead**: File watching uses minimal system resources
- **Efficient Polling**: Only checks file modification time
- **Smart Reloading**: Only reinitializes systems that actually changed

## Troubleshooting

### Hot Reload Not Working
1. **Check File Path**: Ensure `config.json` is in the correct location
2. **File Permissions**: Verify the application can read the config file
3. **JSON Syntax**: Ensure the JSON is valid (use a JSON validator)

### Logging Changes Not Visible
1. **Log Level**: Make sure the new log level is lower than the current messages
2. **Console Output**: Check if console logging is enabled
3. **File Output**: Check if file logging is enabled and directory exists

### Performance Issues
1. **Polling Interval**: Can be adjusted in `FileWatcher.cpp`
2. **File Size**: Large config files may cause slight delays
3. **System Load**: High system load may affect file watching responsiveness

## Future Enhancements

### Planned Features
- **Full Window Hot Reloading**: Real-time window size, title, and property changes
- **Audio System Hot Reloading**: Real-time audio configuration changes
- **Graphics System Hot Reloading**: Real-time graphics quality settings
- **Input System Hot Reloading**: Real-time input sensitivity and mapping changes

### Advanced Features
- **Multiple Config Files**: Watch multiple configuration files
- **Conditional Hot Reloading**: Enable/disable hot reloading for specific systems
- **Hot Reload Profiles**: Different hot reload settings for debug/release builds

## Best Practices

1. **Test Changes**: Always test configuration changes in a safe environment
2. **Backup Config**: Keep a backup of your working configuration
3. **Incremental Changes**: Make small changes to test hot reloading
4. **Monitor Output**: Watch the console for hot reload messages
5. **Validate JSON**: Use a JSON validator to ensure syntax is correct

## Example Workflow

1. **Start Application**: Run your application normally
2. **Make Small Change**: Change one setting in `config.json`
3. **Save File**: Save the file (this triggers hot reload)
4. **Verify Change**: Check console output and application behavior
5. **Repeat**: Make additional changes as needed

The hot reload system makes development much more efficient by eliminating the need to restart the application for every configuration change! 🚀