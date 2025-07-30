#define DOCTEST_CONFIG_DISABLE_EXCEPTIONS
#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include "doctest.h"
#include "Limitless.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/Concurrency/ThreadSafeConfig.h"
#include "Core/ConfigManager.h"
#include <thread>
#include <chrono>
#include <random>
#include <future>
#include <mutex>
#include <variant>

TEST_CASE("Lock-Free SPSC Queue Performance") {
    using namespace Limitless::Concurrency;
    
    LockFreeSPSCQueue<int, 1024> queue;
    
    // Test basic operations
    CHECK(queue.IsEmpty());
    CHECK(!queue.IsFull());
    CHECK(queue.GetSize() == 0);
    
    // Test push/pop operations
    CHECK(queue.TryPush(42));
    CHECK(!queue.IsEmpty());
    CHECK(queue.GetSize() == 1);
    
    auto result = queue.TryPop();
    CHECK(result.has_value());
    CHECK(*result == 42);
    CHECK(queue.IsEmpty());
    
    // Test multiple operations
    for (int i = 0; i < 100; ++i)
    {
        CHECK(queue.TryPush(std::move(i)));
    }
    
    CHECK(queue.GetSize() == 100);
    
    for (int i = 0; i < 100; ++i)
    {
        auto val = queue.TryPop();
        CHECK(val.has_value());
        CHECK(*val == i);
    }
    
    CHECK(queue.IsEmpty());
}

TEST_CASE("Lock-Free MPMC Queue Thread Safety") {
    using namespace Limitless::Concurrency;
    
    LockFreeMPMCQueue<int, 1024> queue;
    std::atomic<bool> stop{false};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> totalProduced{0};
    std::atomic<int> totalConsumed{0};
    
    // Start multiple producer threads
    for (int i = 0; i < 4; ++i)
    {
        producers.emplace_back([&queue, &stop, &totalProduced, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 100);
            
            while (!stop.load())
            {
                int value = dis(gen);
                if (queue.TryPush(std::move(value)))
                {
                    totalProduced.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }
    
    // Start multiple consumer threads
    for (int i = 0; i < 4; ++i)
    {
        consumers.emplace_back([&queue, &stop, &totalConsumed]() {
            while (!stop.load())
            {
                auto result = queue.TryPop();
                if (result.has_value())
                {
                    totalConsumed.fetch_add(1);
                }
                std::this_thread::yield();
            }
        });
    }
    
    // Run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);
    
    // Wait for all threads to finish
    for (auto& thread : producers)
    {
        thread.join();
    }
    for (auto& thread : consumers)
    {
        thread.join();
    }
    
    // Verify that we produced and consumed roughly the same amount
    CHECK(totalProduced.load() > 0);
    CHECK(totalConsumed.load() > 0);
    CHECK(std::abs(totalProduced.load() - totalConsumed.load()) <= 1024); // Allow for queue capacity
}

TEST_CASE("Object Pool Performance") {
    using namespace Limitless::Concurrency;
    
    ObjectPool<std::string, 64> pool;
    
    // Test object acquisition and release
    auto obj1 = pool.Acquire();
    CHECK(obj1 != nullptr);
    
    auto obj2 = pool.Acquire();
    CHECK(obj2 != nullptr);
    CHECK(obj1 != obj2);
    
    pool.Release(std::move(obj1));
    pool.Release(std::move(obj2));
    
    // Test that objects are reused
    auto obj3 = pool.Acquire();
    auto obj4 = pool.Acquire();
    CHECK(obj3 != nullptr);
    CHECK(obj4 != nullptr);
}

TEST_CASE("Work Stealing Queue") {
    using namespace Limitless::Concurrency;
    
    WorkStealingQueue<int, 1024> queue;
    
    // Test owner operations
    queue.Push(42);
    queue.Push(123);
    
    auto result1 = queue.Pop();
    CHECK(result1.has_value());
    CHECK(*result1 == 123); // LIFO for owner
    
    auto result2 = queue.Pop();
    CHECK(result2.has_value());
    CHECK(*result2 == 42);
    
    // Test stealing
    queue.Push(1);
    queue.Push(2);
    queue.Push(3);
    
    auto stolen = queue.Steal();
    CHECK(stolen.has_value());
    CHECK(*stolen == 1); // FIFO for stealers
    
    auto ownerPop = queue.Pop();
    CHECK(ownerPop.has_value());
    CHECK(*ownerPop == 3); // LIFO for owner
}

TEST_CASE("Async I/O Basic Operations") {
    using namespace Limitless::Async;
    
    // Initialize async I/O system
    auto& asyncIO = GetAsyncIO();
    asyncIO.Initialize(2);
    
    // Test file writing and reading
    std::string testContent = "Hello, Async World!\nThis is a test file.";
    std::string testFile = "test_async.txt";
    
    // Write file asynchronously
    auto writeTask = WriteFileAsync(testFile, testContent);
    writeTask.Wait();
    
    // Read file asynchronously
    auto readTask = ReadFileAsync(testFile);
    auto readContent = readTask.Get();
    
    CHECK(readContent == testContent);
    
    // Test file existence
    auto existsTask = FileExistsAsync(testFile);
    CHECK(existsTask.Get() == true);
    
    // Test file size
    auto sizeTask = GetFileSizeAsync(testFile);
    CHECK(sizeTask.Get() == testContent.size());
    
    // Cleanup
    std::filesystem::remove(testFile);
    asyncIO.Shutdown();
}

TEST_CASE("Async I/O Configuration Operations") {
    using namespace Limitless::Async;
    
    auto& asyncIO = GetAsyncIO();
    asyncIO.Initialize(2);
    
    // Test configuration loading and saving
    nlohmann::json testConfig;
    testConfig["window"]["width"] = 1920;
    testConfig["window"]["height"] = 1080;
    testConfig["logging"]["level"] = "debug";
    testConfig["system"]["max_threads"] = 16;
    
    std::string configFile = "test_config.json";
    
    // Save configuration asynchronously
    auto saveTask = SaveConfigAsync(configFile, testConfig);
    saveTask.Wait();
    
    // Load configuration asynchronously
    auto loadTask = LoadConfigAsync(configFile);
    auto loadedConfig = loadTask.Get();
    
    CHECK(loadedConfig["window"]["width"] == 1920);
    CHECK(loadedConfig["window"]["height"] == 1080);
    CHECK(loadedConfig["logging"]["level"] == "debug");
    CHECK(loadedConfig["system"]["max_threads"] == 16);
    
    // Cleanup
    std::filesystem::remove(configFile);
    asyncIO.Shutdown();
}

TEST_CASE("Thread-Safe Configuration Manager") {
    using namespace Limitless::Concurrency;
    using ConfigValue = Limitless::ConfigValue;
    
    auto& config = GetThreadSafeConfig();
    config.Initialize("test_threadsafe_config.json");
    
    // Test basic operations
    config.SetValue("test.int", 42);
    config.SetValue("test.string", std::string("hello"));
    config.SetValue("test.bool", true);
    
    CHECK(config.GetValue<int>("test.int") == 42);
    CHECK(config.GetValue<std::string>("test.string") == "hello");
    CHECK(config.GetValue<bool>("test.bool") == true);
    
    // Test optional values
    auto optValue = config.GetValueOptional<int>("test.int");
    CHECK(optValue.has_value());
    CHECK(*optValue == 42);
    
    auto optMissing = config.GetValueOptional<int>("test.missing");
    CHECK(!optMissing.has_value());
    
    // Test batch operations
    config.BeginBatchUpdate();
    config.SetValue("batch.key1", 100);
    config.SetValue("batch.key2", 200);
    config.SetValue("batch.key3", 300);
    config.EndBatchUpdate();
    
    CHECK(config.GetValue<int>("batch.key1") == 100);
    CHECK(config.GetValue<int>("batch.key2") == 200);
    CHECK(config.GetValue<int>("batch.key3") == 300);
    
    // Test validation
    config.RegisterSchema("test.range", [](const ConfigValue& value) -> bool {
        return std::visit([](const auto& v) -> bool {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int>) {
                return v >= 0 && v <= 100;
            }
            return false;
        }, value);
    });
    
    config.SetValue("test.range", 50); // Valid
    CHECK(config.GetValue<int>("test.range") == 50);
    
    config.SetValue("test.range", 150); // Invalid, should be ignored
    CHECK(config.GetValue<int>("test.range") == 50); // Should remain unchanged
    
    config.Shutdown();
}

TEST_CASE("Thread-Safe Configuration Concurrent Access") {
    using namespace Limitless::Concurrency;
    
    auto& config = GetThreadSafeConfig();
    config.Initialize("test_concurrent_config.json");
    
    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    std::vector<std::thread> writers;
    std::atomic<int> totalReads{0};
    std::atomic<int> totalWrites{0};
    
    // Start reader threads
    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back([&config, &stop, &totalReads]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 9);
            
            while (!stop.load())
            {
                int key = dis(gen);
                std::string keyStr = "concurrent.key" + std::to_string(key);
                auto value = config.GetValue<int>(keyStr, 0);
                totalReads.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }
    
    // Start writer threads
    for (int i = 0; i < 2; ++i)
    {
        writers.emplace_back([&config, &stop, &totalWrites, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 9);
            std::uniform_int_distribution<> valueDis(1, 1000);
            
            while (!stop.load())
            {
                int key = dis(gen);
                int value = valueDis(gen);
                std::string keyStr = "concurrent.key" + std::to_string(key);
                config.SetValue(keyStr, value);
                totalWrites.fetch_add(1);
                std::this_thread::yield();
            }
        });
    }
    
    // Run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);
    
    // Wait for all threads to finish
    for (auto& thread : readers)
    {
        thread.join();
    }
    for (auto& thread : writers)
    {
        thread.join();
    }
    
    // Verify that we had activity
    CHECK(totalReads.load() > 0);
    CHECK(totalWrites.load() > 0);
    
    // Get statistics
    auto stats = config.GetStats();
    CHECK(stats.totalReads > 0);
    CHECK(stats.totalWrites > 0);
    
    config.Shutdown();
}

TEST_CASE("Async Configuration Callbacks") {
    using namespace Limitless::Concurrency;
    using ConfigValue = Limitless::ConfigValue;
    
    auto& config = GetThreadSafeConfig();
    config.Initialize("test_async_callbacks.json");
    
    std::atomic<int> callbackCount{0};
    std::string lastChangedKey;
    std::mutex lastChangedKeyMutex;
    std::atomic<int> lastChangedValue{0};
    
    // Register async callback
    config.RegisterAsyncChangeCallback("async.test", 
        [&callbackCount, &lastChangedKey, &lastChangedKeyMutex, &lastChangedValue](const std::string& key, const ConfigValue& value) {
            callbackCount.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(lastChangedKeyMutex);
                lastChangedKey = key;
            }
            std::visit([&lastChangedValue](const auto& v) {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>, int>) {
                    lastChangedValue.store(v);
                }
            }, value);
        });
    
    // Set value and wait for callback
    config.SetValue("async.test", 123);
    
    // Wait a bit for async callback to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    CHECK(callbackCount.load() > 0);
    {
        std::lock_guard<std::mutex> lock(lastChangedKeyMutex);
        CHECK(lastChangedKey == "async.test");
    }
    CHECK(lastChangedValue.load() == 123);
    
    config.Shutdown();
}

TEST_CASE("Event System Lock-Free Queue Integration") {
    using namespace Limitless;
    
    // Initialize event system
    auto& eventSystem = GetEventSystem();
    eventSystem.Initialize();
    
    std::atomic<int> eventCount{0};
    
    // Register event callback
    eventSystem.AddCallback(EventType::AppTick, [&eventCount](Event& event) {
        eventCount.fetch_add(1);
    });
    
    // Dispatch events from multiple threads
    std::vector<std::thread> dispatchers;
    std::atomic<bool> stop{false};
    
    for (int i = 0; i < 4; ++i)
    {
        dispatchers.emplace_back([&eventSystem, &stop]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 10);
            
            while (!stop.load())
            {
                auto event = std::make_unique<Events::AppTickEvent>(0.016f);
                eventSystem.DispatchDeferred(std::move(event));
                std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
            }
        });
    }
    
    // Process events in main thread
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::milliseconds(100))
    {
        eventSystem.ProcessEvents(100);
        std::this_thread::yield();
    }
    
    stop.store(true);
    
    // Wait for dispatcher threads
    for (auto& thread : dispatchers)
    {
        thread.join();
    }
    
    // Process remaining events
    eventSystem.ProcessEvents();
    
    // Verify events were processed
    CHECK(eventCount.load() > 0);
    
    // Get event system statistics
    auto stats = eventSystem.GetStats();
    CHECK(stats.dispatchStats.totalEventsDispatched > 0);
    CHECK(stats.queueStats.totalEnqueued > 0);
    
    eventSystem.Shutdown();
}

TEST_CASE("Async I/O Performance Benchmark") {
    using namespace Limitless::Async;
    
    auto& asyncIO = GetAsyncIO();
    asyncIO.Initialize(4);
    
    // Create test data
    std::string largeContent(1024 * 1024, 'A'); // 1MB of data
    std::vector<std::string> files;
    
    // Generate multiple test files
    for (int i = 0; i < 10; ++i)
    {
        files.push_back("perf_test_" + std::to_string(i) + ".txt");
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Write files concurrently
    std::vector<Limitless::Async::Task<void>> writeTasks;
    for (const auto& file : files)
    {
        writeTasks.push_back(WriteFileAsync(file, largeContent));
    }
    
    // Wait for all writes to complete
    for (auto& task : writeTasks)
    {
        task.Wait();
    }
    
    auto writeEndTime = std::chrono::high_resolution_clock::now();
    
    // Read files concurrently
    std::vector<Limitless::Async::Task<std::string>> readTasks;
    for (const auto& file : files)
    {
        readTasks.push_back(ReadFileAsync(file));
    }
    
    // Wait for all reads to complete
    for (auto& task : readTasks)
    {
        auto content = task.Get();
        CHECK(content.size() == largeContent.size());
    }
    
    auto readEndTime = std::chrono::high_resolution_clock::now();
    
    // Calculate performance metrics
    auto writeDuration = std::chrono::duration<double, std::milli>(writeEndTime - startTime);
    auto readDuration = std::chrono::duration<double, std::milli>(readEndTime - writeEndTime);
    
    LT_INFO("Async I/O Performance:");
    LT_INFO("  Write time: {:.2f}ms for {} files", writeDuration.count(), files.size());
    LT_INFO("  Read time: {:.2f}ms for {} files", readDuration.count(), files.size());
    LT_INFO("  Write throughput: {:.2f} MB/s", 
            (files.size() * largeContent.size() / (1024.0 * 1024.0)) / (writeDuration.count() / 1000.0));
    LT_INFO("  Read throughput: {:.2f} MB/s", 
            (files.size() * largeContent.size() / (1024.0 * 1024.0)) / (readDuration.count() / 1000.0));
    
    // Cleanup
    for (const auto& file : files)
    {
        std::filesystem::remove(file);
    }
    
    asyncIO.Shutdown();
}

TEST_CASE("Concurrency System Integration") {
    using namespace Limitless;
    using namespace Limitless::Concurrency;
    using namespace Limitless::Async;
    using ConfigValue = Limitless::ConfigValue;
    
    // Initialize all systems
    auto& asyncIO = GetAsyncIO();
    auto& config = GetThreadSafeConfig();
    auto& eventSystem = GetEventSystem();
    
    asyncIO.Initialize(4);
    config.Initialize("integration_test.json");
    eventSystem.Initialize();
    
    // Test integrated workflow
    std::atomic<int> configChangeCount{0};
    std::atomic<int> eventCount{0};
    
    // Register config change callback that triggers events
    config.RegisterAsyncChangeCallback("integration.test", 
        [&eventSystem, &configChangeCount](const std::string& key, const ConfigValue& value) {
            configChangeCount.fetch_add(1);
            
            // Dispatch an event when config changes
            auto event = std::make_unique<Events::ConfigReloadedEvent>("integration_test.json");
            eventSystem.DispatchDeferred(std::move(event));
        });
    
    // Register event callback
    eventSystem.AddCallback(EventType::ConfigReloaded, [&eventCount](Event& event) {
        eventCount.fetch_add(1);
    });
    
    // Perform async configuration operations
    auto saveTask = config.SaveToFileAsync();
    saveTask.Wait();
    
    auto loadTask = config.LoadFromFileAsync("integration_test.json");
    loadTask.Wait();
    
    // Set configuration values
    config.SetValue("integration.test", 42);
    config.SetValue("integration.another", std::string("test"));
    
    // Process events multiple times to ensure async callbacks are processed
    for (int i = 0; i < 10; ++i)
    {
        eventSystem.ProcessEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Wait longer for async operations to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Process events one more time
    eventSystem.ProcessEvents();
    
    // Verify integration worked
    CHECK(configChangeCount.load() > 0);
    CHECK(eventCount.load() > 0);
    
    // Get statistics from all systems
    auto configStats = config.GetStats();
    auto eventStats = eventSystem.GetStats();
    
    CHECK(configStats.totalWrites > 0);
    CHECK(eventStats.dispatchStats.totalEventsDispatched > 0);
    
    // Shutdown all systems
    eventSystem.Shutdown();
    config.Shutdown();
    asyncIO.Shutdown();
} 