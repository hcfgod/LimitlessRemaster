# Concurrency and AsyncIO Integration Guide

## Overview

The Limitless Engine provides a comprehensive concurrency system with AsyncIO capabilities for efficient file operations. This guide covers how to use the AsyncIO system throughout the codebase.

## AsyncIO System

### Initialization

When using `Limitless::Application`, the AsyncIO system is initialized early using `system.max_threads` from `config.json`:

```cpp
auto& asyncIO = Limitless::Async::GetAsyncIO();
size_t threadCount = configManager.GetValue<size_t>("system.max_threads", 0);
asyncIO.Initialize(threadCount);
```

If you use `AsyncIO` outside `Limitless::Application`, call `Initialize()` yourself (passing `0` selects a sensible default):

```cpp
auto& asyncIO = Limitless::Async::GetAsyncIO();
asyncIO.Initialize(0);
```

### Configuration

Add AsyncIO settings to your `config.json`:

```json
{
  "system": {
    "max_threads": 8
  }
}
```

Note: the internal AsyncIO work queue capacity is currently a runtime limit (bounded queue) and may be adjusted in code.

### Result-returning APIs (recommended)

AsyncIO provides `Task<Result<T>>` variants for engine code so failures are returned explicitly instead of throwing across the async boundary:

- `ReadFileAsyncResult`
- `WriteFileAsyncResult`
- `AppendFileAsyncResult`
- `LoadConfigAsyncResult`
- `SaveConfigAsyncResult`

## File Operations

### Basic File Operations

```cpp
auto& asyncIO = Limitless::Async::GetAsyncIO();

// Read file asynchronously
auto readTask = asyncIO.ReadFileAsync("config.json");
std::string content = readTask.Get(); // Wait for completion

// Write file asynchronously
auto writeTask = asyncIO.WriteFileAsync("output.txt", "Hello World!");
writeTask.Wait(); // Wait for completion

// Check if file exists
auto existsTask = asyncIO.FileExistsAsync("file.txt");
bool exists = existsTask.Get();

// Get file size
auto sizeTask = asyncIO.GetFileSizeAsync("large_file.dat");
size_t size = sizeTask.Get();
```

### Configuration Operations

```cpp
// Save configuration asynchronously
nlohmann::json config;
config["setting"] = "value";
config["number"] = 42;

auto saveTask = asyncIO.SaveConfigAsync("settings.json", config);
saveTask.Wait();

// Load configuration asynchronously
auto loadTask = asyncIO.LoadConfigAsync("settings.json");
nlohmann::json loadedConfig = loadTask.Get();
```

### Directory Operations

```cpp
// Create directory
auto createTask = asyncIO.CreateDirectoryAsync("new_folder");
bool created = createTask.Get();

// List directory contents
auto listTask = asyncIO.ListDirectoryAsync(".");
std::vector<std::string> files = listTask.Get();

// Delete file
auto deleteFileTask = asyncIO.DeleteFileAsync("temp.txt");
bool deleted = deleteFileTask.Get();

// Delete directory
auto deleteDirTask = asyncIO.DeleteDirectoryAsync("temp_folder");
bool dirDeleted = deleteDirTask.Get();
```

## Concurrent Operations

### Multiple File Operations

```cpp
auto& asyncIO = Limitless::Async::GetAsyncIO();

// Start multiple operations concurrently
std::vector<Limitless::Async::Task<std::string>> readTasks;
std::vector<Limitless::Async::Task<void>> writeTasks;

for (int i = 0; i < 10; ++i)
{
    std::string filename = "file_" + std::to_string(i) + ".txt";
    std::string content = "Content for file " + std::to_string(i);
    
    writeTasks.push_back(asyncIO.WriteFileAsync(filename, content));
    readTasks.push_back(asyncIO.ReadFileAsync(filename));
}

// Wait for all write operations to complete
for (auto& task : writeTasks)
{
    task.Wait();
}

// Process all read results
for (auto& task : readTasks)
{
    std::string content = task.Get();
    // Process content...
}
```

### Batch Processing

```cpp
// Process multiple files in batches
std::vector<std::string> files = {"file1.txt", "file2.txt", "file3.txt"};
std::vector<Limitless::Async::Task<std::string>> tasks;

for (const auto& file : files)
{
    tasks.push_back(asyncIO.ReadFileAsync(file));
}

// Wait for all tasks and process results
for (auto& task : tasks)
{
    std::string content = task.Get();
    // Process each file's content...
}
```

## Implementation Notes (What’s actually happening today)

- AsyncIO executes work on a **dedicated thread pool** and returns future-backed `Task<T>` results.
- AsyncIO’s internal work queue is a **bounded, blocking MPMC queue** (mutex + condition variable). This is an intentional correctness choice.
- The engine also provides lock-free queues in `Core/Concurrency/LockFreeQueue.h` (SPSC + bounded MPMC ring buffer). These are used in some subsystems for fast producer/consumer handoff.

## Correctness Contracts (Threading + Ownership Invariants)

This section defines **hard invariants** for the concurrency primitives used by the engine. Treat these as API contracts.

### `LockFreeSPSCQueue<T, Size>` (Single Producer / Single Consumer)

File: `Limitless/Source/Core/Concurrency/LockFreeQueue.h`

- **Thread model**
  - Exactly **one producer thread** may call `TryPush()`.
  - Exactly **one consumer thread** may call `TryPop()`.
  - `IsEmpty()/IsFull()/GetSize()` are safe to call from either of those two threads, but the size is **approximate** under contention.
- **Capacity**
  - The effective capacity is **`Size - 1`** items (one slot is intentionally left empty to disambiguate full vs empty).
- **Ownership**
  - `TryPush(T&&)` moves `item` into the ring buffer.
  - `TryPop()` moves an item out and returns it as `std::optional<T>`.
- **Lifetime / safety**
  - `Clear()` is **not thread-safe** and must only be called when neither producer nor consumer is operating.
  - `T` must be nothrow-moveable (enforced via `static_assert`).

### `LockFreeMPMCQueue<T, Size>` (Multiple Producer / Multiple Consumer)

File: `Limitless/Source/Core/Concurrency/LockFreeQueue.h`

- **Thread model**
  - Multiple producers may call `TryPush()` concurrently.
  - Multiple consumers may call `TryPop()` concurrently.
- **Capacity**
  - The queue is bounded to **`Size`** items (fixed ring buffer).
  - `TryPush()` returns `false` when full; `TryPop()` returns `std::nullopt` when empty.
- **Ownership**
  - Items are moved into/out of the queue; `T` must be default constructible and nothrow-moveable (enforced).
- **Lifetime / safety**
  - `Clear()` is **not thread-safe** and must only be called when no other thread is pushing/popping.

### Work-stealing queue (`WorkStealingQueue<T, Size>`)

- **Thread model**
  - `Push()` and `Pop()` are **owner-thread only**.
  - `Steal()` may be called by other threads.

### Where these queues are used

- **Event queue**: `EventQueue` uses a bounded `LockFreeMPMCQueue` for `DispatchDeferred()` → `ProcessEvents()` handoff.
- **Render queue**: `RenderCommandQueue` uses a bounded `LockFreeMPMCQueue` for multi-thread submit → single-thread execute.

## Integration Points

### ConfigManager Integration

The ConfigManager automatically uses AsyncIO for all file operations:

```cpp
auto& config = Limitless::ConfigManager::GetInstance();

// These operations use AsyncIO internally
config.LoadFromFile("config.json");
config.SaveToFile("config.json");

// Async versions are also available
auto loadTask = config.LoadFromFileAsync("config.json");
auto saveTask = config.SaveToFileAsync("config.json");
```

### FileWatcher Integration

The FileWatcher uses AsyncIO for file system operations:

```cpp
auto& fileWatcher = std::make_unique<Limitless::FileWatcher>();
fileWatcher->StartWatching("config.json", [](const std::string& filepath) {
    // File changed - reload configuration
    auto& config = Limitless::ConfigManager::GetInstance();
    config.ReloadFromFile();
});
```

### Logging Integration

The logging system uses **spdlog async logging** (its own internal async thread pool). It does not route log file writes through AsyncIO.

## Best Practices

### 1. Always Initialize AsyncIO

Ensure AsyncIO is initialized before using any file operations:

```cpp
// Check if AsyncIO is available
auto& asyncIO = Limitless::Async::GetAsyncIO();
if (!asyncIO.IsInitialized())
{
    LT_ERROR("AsyncIO not initialized!");
    return;
}
```

### 2. Handle Exceptions

Always handle exceptions from async operations:

```cpp
try
{
    auto task = asyncIO.ReadFileAsync("file.txt");
    std::string content = task.Get();
}
catch (const std::exception& e)
{
    LT_ERROR("Failed to read file: {}", e.what());
}
```

### 3. Use Appropriate Thread Count

Configure the thread count based on your system:

```cpp
// Use hardware concurrency or a reasonable default
size_t threadCount = std::thread::hardware_concurrency();
if (threadCount == 0) threadCount = 4; // Fallback

asyncIO.Initialize(threadCount);
```

### 4. Batch Operations

Group related operations together for better performance:

```cpp
// Good: Batch multiple operations
std::vector<Limitless::Async::Task<void>> tasks;
for (const auto& file : files)
{
    tasks.push_back(asyncIO.DeleteFileAsync(file));
}

// Wait for all to complete
for (auto& task : tasks)
{
    task.Wait();
}
```

### 5. Avoid Blocking

Don't block the main thread unnecessarily:

```cpp
// Good: Non-blocking approach
auto task = asyncIO.ReadFileAsync("large_file.txt");
// Do other work while file is being read
// ...
std::string content = task.Get(); // Only block when you need the result
```

## Performance Considerations

### Thread Pool Sizing

- **Small applications**: 2-4 threads
- **Medium applications**: 4-8 threads  
- **Large applications**: 8-16 threads
- **I/O intensive**: Consider using more threads

### Queue Size

The default queue size (8192) is suitable for most applications. Increase if you have many concurrent file operations:

```cpp
// For high-throughput applications
constexpr size_t QUEUE_SIZE = 16384;
```

### Memory Usage

AsyncIO operations use memory for:
- Task queue storage
- Thread stack space
- Temporary buffers

Monitor memory usage in memory-constrained environments.

## Error Handling

### Common Error Scenarios

```cpp
// File not found
try
{
    auto task = asyncIO.ReadFileAsync("nonexistent.txt");
    std::string content = task.Get();
}
catch (const std::runtime_error& e)
{
    LT_ERROR("File not found: {}", e.what());
}

// Permission denied
try
{
    auto task = asyncIO.WriteFileAsync("/system/readonly.txt", "data");
    task.Wait();
}
catch (const std::runtime_error& e)
{
    LT_ERROR("Permission denied: {}", e.what());
}

// Disk full
try
{
    auto task = asyncIO.WriteFileAsync("large_file.dat", largeData);
    task.Wait();
}
catch (const std::runtime_error& e)
{
    LT_ERROR("Disk full: {}", e.what());
}
```

### Recovery Strategies

```cpp
// Retry with exponential backoff
auto readWithRetry = [&asyncIO](const std::string& filename, int maxRetries = 3) {
    for (int attempt = 0; attempt < maxRetries; ++attempt)
    {
        try
        {
            auto task = asyncIO.ReadFileAsync(filename);
            return task.Get();
        }
        catch (const std::exception& e)
        {
            if (attempt == maxRetries - 1)
                throw;
            
            // Wait before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << attempt)));
        }
    }
    return std::string();
};
```

## Testing AsyncIO

### Unit Tests

```cpp
TEST_CASE("AsyncIO Basic Operations")
{
    auto& asyncIO = Limitless::Async::GetAsyncIO();
    asyncIO.Initialize(2);
    
    // Test file operations
    auto writeTask = asyncIO.WriteFileAsync("test.txt", "Hello");
    writeTask.Wait();
    
    auto readTask = asyncIO.ReadFileAsync("test.txt");
    std::string content = readTask.Get();
    CHECK(content == "Hello");
    
    // Cleanup
    asyncIO.DeleteFileAsync("test.txt").Wait();
    asyncIO.Shutdown();
}
```

### Performance Tests

```cpp
TEST_CASE("AsyncIO Performance")
{
    auto& asyncIO = Limitless::Async::GetAsyncIO();
    asyncIO.Initialize(4);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Perform multiple concurrent operations
    std::vector<Limitless::Async::Task<void>> tasks;
    for (int i = 0; i < 100; ++i)
    {
        std::string filename = "perf_test_" + std::to_string(i) + ".txt";
        tasks.push_back(asyncIO.WriteFileAsync(filename, "test data"));
    }
    
    for (auto& task : tasks)
    {
        task.Wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    LT_INFO("Completed 100 file operations in {} ms", duration.count());
    
    // Cleanup
    for (int i = 0; i < 100; ++i)
    {
        std::string filename = "perf_test_" + std::to_string(i) + ".txt";
        asyncIO.DeleteFileAsync(filename).Wait();
    }
    
    asyncIO.Shutdown();
}
```

## Integration Checklist

- [ ] AsyncIO initialized in main entry point
- [ ] ConfigManager using AsyncIO for file operations
- [ ] FileWatcher using AsyncIO for file system operations
- [ ] Logging system configured for AsyncIO
- [ ] Error handling implemented for async operations
- [ ] Thread count configured appropriately
- [ ] Performance testing completed
- [ ] Documentation updated

## Troubleshooting

### Common Issues

1. **AsyncIO not initialized**: Ensure AsyncIO is initialized before use
2. **Thread pool exhausted**: Increase thread count or reduce concurrent operations
3. **Queue full**: Increase queue size or reduce operation frequency
4. **File permission errors**: Check file permissions and paths
5. **Memory leaks**: Ensure proper cleanup of Task objects

### Debug Information

Enable debug logging to troubleshoot AsyncIO issues:

```cpp
// In config.json
{
  "logging": {
    "level": "debug"
  }
}
```

This will show detailed AsyncIO operation logs including thread usage, queue status, and operation timing. 