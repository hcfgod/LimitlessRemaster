#define DOCTEST_CONFIG_DISABLE_EXCEPTIONS
#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Core/Concurrency/AsyncIO.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

TEST_SUITE("Async I/O System")
{
    TEST_CASE("Basic Async I/O Operations")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        
        // Initialize async I/O system
        asyncIO.Initialize(4); // 4 worker threads
        
        // Test basic file operations
        const std::string testFile = "async_test_file.txt";
        const std::string testContent = "Hello, Async I/O World!\nThis is a test file.\n";
        
        // Write file asynchronously
        auto writeTask = asyncIO.WriteFileAsync(testFile, testContent);
        writeTask.Wait();
        
        // Verify file was created
        CHECK(std::filesystem::exists(testFile) == true);
        
        // Read file asynchronously
        auto readTask = asyncIO.ReadFileAsync(testFile);
        std::string readContent = readTask.Get();
        
        // Verify content matches
        CHECK(readContent == testContent);
        
        // Test append operation
        const std::string appendContent = "Appended content!\n";
        auto appendTask = asyncIO.AppendFileAsync(testFile, appendContent);
        appendTask.Wait();
        
        // Read again to verify append
        auto readAgainTask = asyncIO.ReadFileAsync(testFile);
        std::string fullContent = readAgainTask.Get();
        CHECK(fullContent == testContent + appendContent);
        
        // Test file existence check
        auto existsTask = asyncIO.FileExistsAsync(testFile);
        bool exists = existsTask.Get();
        CHECK(exists == true);
        
        // Test file size
        auto sizeTask = asyncIO.GetFileSizeAsync(testFile);
        size_t fileSize = sizeTask.Get();
        CHECK(fileSize > 0);
        CHECK(fileSize == fullContent.length());
        
        // Cleanup
        std::filesystem::remove(testFile);
        asyncIO.Shutdown();
    }
    
    TEST_CASE("Concurrent File Operations")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Initialize(8); // More threads for concurrent testing
        
        const int numFiles = 10;
        std::vector<std::string> filenames;
        std::vector<std::string> contents;
        std::vector<Limitless::Async::Task<void>> writeTasks;
        std::vector<Limitless::Async::Task<std::string>> readTasks;
        
        // Prepare test data
        for (int i = 0; i < numFiles; ++i)
        {
            filenames.push_back("concurrent_test_" + std::to_string(i) + ".txt");
            contents.push_back("File " + std::to_string(i) + " content\n");
        }
        
        // Start concurrent write operations
        for (int i = 0; i < numFiles; ++i)
        {
            writeTasks.push_back(asyncIO.WriteFileAsync(filenames[i], contents[i]));
        }
        
        // Wait for all writes to complete
        for (auto& task : writeTasks)
        {
            task.Wait();
        }
        
        // Start concurrent read operations
        for (int i = 0; i < numFiles; ++i)
        {
            readTasks.push_back(asyncIO.ReadFileAsync(filenames[i]));
        }
        
        // Verify all reads
        for (int i = 0; i < numFiles; ++i)
        {
            std::string readContent = readTasks[i].Get();
            CHECK(readContent == contents[i]);
        }
        
        // Cleanup
        for (const auto& filename : filenames)
        {
            std::filesystem::remove(filename);
        }
        
        asyncIO.Shutdown();
    }
    
    TEST_CASE("Directory Operations")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Initialize(4);
        
        const std::string testDir = "async_test_directory";
        
        // Create directory
        auto createDirTask = asyncIO.CreateDirectoryAsync(testDir);
        bool dirCreated = createDirTask.Get();
        CHECK(dirCreated == true);
        CHECK(std::filesystem::exists(testDir) == true);
        
        // Create some files in the directory
        for (int i = 0; i < 5; ++i)
        {
            std::string filename = testDir + "/file_" + std::to_string(i) + ".txt";
            auto writeTask = asyncIO.WriteFileAsync(filename, "Test file " + std::to_string(i));
            writeTask.Wait();
        }
        
        // List directory contents
        auto listDirTask = asyncIO.ListDirectoryAsync(testDir);
        std::vector<std::string> files = listDirTask.Get();
        CHECK(files.size() >= 5);
        
        // Check if directory exists using filesystem
        bool dirExists = std::filesystem::exists(testDir);
        CHECK(dirExists == true);
        
        // Get directory size using filesystem
        size_t dirSize = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(testDir))
        {
            if (std::filesystem::is_regular_file(entry))
            {
                dirSize += std::filesystem::file_size(entry);
            }
        }
        CHECK(dirSize > 0);
        
        // Cleanup
        std::filesystem::remove_all(testDir);
        asyncIO.Shutdown();
    }
    
    TEST_CASE("Lock-Free Queue Operations")
    {
        // Test SPSC (Single Producer, Single Consumer) queue
        Limitless::Concurrency::LockFreeSPSCQueue<int, 128> spscQueue;
        
        // Single producer test
        for (int i = 0; i < 50; ++i)
        {
            bool pushed = spscQueue.TryPush(std::move(i));
            CHECK(pushed == true);
        }
        
        // Single consumer test
        for (int i = 0; i < 50; ++i)
        {
            std::optional<int> value = spscQueue.TryPop();
            CHECK(value.has_value());
            CHECK(value.value() == i);
        }
        
        // Test MPMC (Multiple Producer, Multiple Consumer) queue
        Limitless::Concurrency::LockFreeMPMCQueue<int, 128> mpmcQueue;
        
        const int numThreads = 4;
        const int itemsPerThread = 25;
        
        std::vector<std::thread> producerThreads;
        std::vector<std::thread> consumerThreads;
        std::atomic<int> totalConsumed{0};
        std::atomic<int> totalProduced{0};
        
        // Start producer threads
        for (int i = 0; i < numThreads; ++i)
        {
            producerThreads.emplace_back([&mpmcQueue, i, itemsPerThread, &totalProduced]() {
                for (int j = 0; j < itemsPerThread; ++j)
                {
                    int value = i * itemsPerThread + j;
                    if (mpmcQueue.TryPush(std::move(value)))
                    {
                        totalProduced++;
                    }
                }
            });
        }
        
        // Start consumer threads
        for (int i = 0; i < numThreads; ++i)
        {
            consumerThreads.emplace_back([&mpmcQueue, &totalConsumed]() {
                while (totalConsumed < numThreads * 25)
                {
                    auto value = mpmcQueue.TryPop();
                    if (value.has_value())
                    {
                        totalConsumed++;
                    }
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : producerThreads)
        {
            thread.join();
        }
        for (auto& thread : consumerThreads)
        {
            thread.join();
        }
        
        // Verify all items were processed
        CHECK(totalProduced == numThreads * itemsPerThread);
        CHECK(totalConsumed == numThreads * itemsPerThread);
    }
    
    TEST_CASE("Async Task System")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Initialize(4);
        
        // Test simple async task
        auto simpleTask = Limitless::Async::Task<int>([&]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 42;
        });
        
        int result = simpleTask.Get();
        CHECK(result == 42);
        
        // Test async task with parameters
        int x = 123;
        std::string str = "Result: ";
        auto paramTask = Limitless::Async::Task<std::string>([x, str]() -> std::string {
            return str + std::to_string(x);
        });
        
        std::string paramResult = paramTask.Get();
        CHECK(paramResult == "Result: 123");
        
        // Test multiple concurrent tasks
        std::vector<Limitless::Async::Task<int>> tasks;
        for (int i = 0; i < 10; ++i)
        {
            tasks.push_back(Limitless::Async::Task<int>([i]() -> int {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return i * i;
            }));
        }
        
        // Wait for all tasks and verify results
        for (int i = 0; i < 10; ++i)
        {
            int taskResult = tasks[i].Get();
            CHECK(taskResult == i * i);
        }
        
        asyncIO.Shutdown();
    }
    
    TEST_CASE("Error Handling")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Initialize(4);
        
        // Test reading non-existent file
        auto readNonExistentTask = asyncIO.ReadFileAsync("non_existent_file.txt");
        try
        {
            std::string content = readNonExistentTask.Get();
            CHECK(false); // Should not reach here
        }
        catch (const std::exception&)
        {
            // Expected exception
            CHECK(true);
        }
        
        // Test writing to invalid path
        auto writeInvalidTask = asyncIO.WriteFileAsync("/invalid/path/file.txt", "test");
        try
        {
            writeInvalidTask.Wait();
            CHECK(false); // Should not reach here
        }
        catch (const std::exception&)
        {
            // Expected exception
            CHECK(true);
        }
        
        asyncIO.Shutdown();
    }
    
    TEST_CASE("Performance Testing")
    {
        auto& asyncIO = Limitless::Async::GetAsyncIO();
        asyncIO.Initialize(8);
        
        const int numOperations = 100;
        const std::string testContent = "Performance test content\n";
        
        // Measure async write performance
        auto writeStart = std::chrono::high_resolution_clock::now();
        std::vector<Limitless::Async::Task<void>> writeTasks;
        
        for (int i = 0; i < numOperations; ++i)
        {
            std::string filename = "perf_test_" + std::to_string(i) + ".txt";
            writeTasks.push_back(asyncIO.WriteFileAsync(filename, testContent));
        }
        
        // Wait for all writes
        for (auto& task : writeTasks)
        {
            task.Wait();
        }
        
        auto writeEnd = std::chrono::high_resolution_clock::now();
        auto writeDuration = std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart);
        
        // Measure async read performance
        auto readStart = std::chrono::high_resolution_clock::now();
        std::vector<Limitless::Async::Task<std::string>> readTasks;
        
        for (int i = 0; i < numOperations; ++i)
        {
            std::string filename = "perf_test_" + std::to_string(i) + ".txt";
            readTasks.push_back(asyncIO.ReadFileAsync(filename));
        }
        
        // Wait for all reads
        for (auto& task : readTasks)
        {
            std::string content = task.Get();
            CHECK(content == testContent);
        }
        
        auto readEnd = std::chrono::high_resolution_clock::now();
        auto readDuration = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart);
        
        // Performance should be reasonable
        double writeTimePerOp = static_cast<double>(writeDuration.count()) / numOperations;
        double readTimePerOp = static_cast<double>(readDuration.count()) / numOperations;
        
        CHECK(writeTimePerOp < 10000.0); // Less than 10ms per write
        CHECK(readTimePerOp < 10000.0);  // Less than 10ms per read
        
        // Cleanup
        for (int i = 0; i < numOperations; ++i)
        {
            std::string filename = "perf_test_" + std::to_string(i) + ".txt";
            std::filesystem::remove(filename);
        }
        
        asyncIO.Shutdown();
    }
} 