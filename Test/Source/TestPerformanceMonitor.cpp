#include "Core/PerformanceMonitor.h"
#include "Core/Debug/Log.h"
#include "Platform/Platform.h"

#include <thread>
#include <chrono>
#include <vector>
#include <random>

using namespace Limitless;

class PerformanceTest {
private:
    PerformanceMonitor& m_monitor;
    std::vector<int> m_testData;
    std::random_device m_rd;
    std::mt19937 m_gen;

public:
    PerformanceTest() 
        : m_monitor(PerformanceMonitor::GetInstance())
        , m_gen(m_rd()) {
        
        // Initialize performance monitor
        m_monitor.Initialize();
        m_monitor.SetLoggingEnabled(true);
        
        // Set up metrics callback
        m_monitor.SetMetricsCallback([this](const PerformanceMetrics& metrics) {
            OnMetricsCollected(metrics);
        });
        
        // Set collection interval to 2 seconds
        m_monitor.SetMetricsCollectionInterval(2.0);
        
        // Generate test data
        GenerateTestData();
        
        LT_LOG_INFO("Performance Test initialized");
    }
    
    ~PerformanceTest() {
        m_monitor.Shutdown();
    }
    
    void RunTest() {
        LT_LOG_INFO("Starting Performance Test...");
        
        // Run different performance tests
        TestFrameTiming();
        TestPerformanceCounters();
        TestMemoryTracking();
        TestCPUIntensiveOperations();
        TestMemoryAllocations();
        
        // Final metrics report
        m_monitor.LogMetrics();
        m_monitor.SaveMetricsToFile("performance_test_report.txt");
        
        LT_LOG_INFO("Performance Test completed");
    }
    
private:
    void GenerateTestData() {
        std::uniform_int_distribution<> dis(1, 1000);
        m_testData.resize(100000);
        for (auto& value : m_testData) {
            value = dis(m_gen);
        }
    }
    
    void TestFrameTiming() {
        LT_LOG_INFO("Testing Frame Timing...");
        
        const int frameCount = 100;
        
        for (int i = 0; i < frameCount; ++i) {
            LT_PERF_BEGIN_FRAME();
            
            // Simulate frame processing
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
            
            // Simulate some frame variation
            if (i % 10 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Occasional spike
            }
            
            LT_PERF_END_FRAME();
            
            // Log every 20 frames
            if (i % 20 == 0) {
                double frameTime = m_monitor.GetFrameTime();
                double fps = m_monitor.GetFPS();
                LT_LOG_INFO("Frame {}: {:.2f}ms ({:.1f} FPS)", i, frameTime, fps);
            }
        }
        
        LT_LOG_INFO("Frame timing test completed. Average FPS: {:.1f}", m_monitor.GetAverageFPS());
    }
    
    void TestPerformanceCounters() {
        LT_LOG_INFO("Testing Performance Counters...");
        
        // Create counters for different operations
        auto* sortCounter = m_monitor.CreateCounter("DataSort");
        auto* searchCounter = m_monitor.CreateCounter("DataSearch");
        auto* computeCounter = m_monitor.CreateCounter("DataCompute");
        
        // Test data sorting
        {
            LT_PERF_COUNTER("DataSort");
            std::vector<int> data = m_testData;
            std::sort(data.begin(), data.end());
        }
        
        // Test data searching
        {
            LT_PERF_COUNTER("DataSearch");
            std::vector<int> data = m_testData;
            std::sort(data.begin(), data.end());
            
            // Perform multiple searches
            for (int i = 0; i < 1000; ++i) {
                std::binary_search(data.begin(), data.end(), i);
            }
        }
        
        // Test data computation
        {
            LT_PERF_COUNTER("DataCompute");
            double sum = 0.0;
            for (int value : m_testData) {
                sum += std::sqrt(value) * std::log(value + 1);
            }
        }
        
        // Log counter results
        LT_LOG_INFO("Sort counter: {:.2f}ms (avg: {:.2f}ms)", 
                   sortCounter->GetLastValue(), sortCounter->GetAverageValue());
        LT_LOG_INFO("Search counter: {:.2f}ms (avg: {:.2f}ms)", 
                   searchCounter->GetLastValue(), searchCounter->GetAverageValue());
        LT_LOG_INFO("Compute counter: {:.2f}ms (avg: {:.2f}ms)", 
                   computeCounter->GetLastValue(), computeCounter->GetAverageValue());
    }
    
    void TestMemoryTracking() {
        LT_LOG_INFO("Testing Memory Tracking...");
        
        auto* tracker = m_monitor.GetMemoryTracker();
        tracker->Reset();
        
        // Simulate memory allocations
        std::vector<std::vector<int>> allocations;
        
        for (int i = 0; i < 100; ++i) {
            size_t size = (i + 1) * 1024; // 1KB to 100KB
            allocations.emplace_back(size / sizeof(int));
            
            // Track the allocation
            LT_PERF_TRACK_MEMORY(size);
            
            // Fill with some data
            allocations.back().assign(size / sizeof(int), i);
        }
        
        LT_LOG_INFO("Memory allocated: {:.2f}MB", 
                   tracker->GetCurrentMemory() / (1024.0 * 1024.0));
        LT_LOG_INFO("Allocation count: {}", tracker->GetAllocationCount());
        
        // Simulate memory deallocations
        for (int i = 0; i < 50; ++i) {
            size_t size = (i + 1) * 1024;
            allocations.pop_back();
            
            // Track the deallocation
            LT_PERF_UNTrack_MEMORY(size);
        }
        
        LT_LOG_INFO("Memory after deallocation: {:.2f}MB", 
                   tracker->GetCurrentMemory() / (1024.0 * 1024.0));
        LT_LOG_INFO("Peak memory: {:.2f}MB", 
                   tracker->GetPeakMemory() / (1024.0 * 1024.0));
        
        // Clean up remaining allocations
        for (auto& alloc : allocations) {
            LT_PERF_UNTrack_MEMORY(alloc.size() * sizeof(int));
        }
    }
    
    void TestCPUIntensiveOperations() {
        LT_LOG_INFO("Testing CPU Intensive Operations...");
        
        auto* cpuCounter = m_monitor.CreateCounter("CPUIntensive");
        
        // Perform CPU-intensive operations
        cpuCounter->Start();
        
        // Matrix multiplication simulation
        const int size = 100;
        std::vector<std::vector<double>> matrix1(size, std::vector<double>(size));
        std::vector<std::vector<double>> matrix2(size, std::vector<double>(size));
        std::vector<std::vector<double>> result(size, std::vector<double>(size));
        
        // Initialize matrices
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                matrix1[i][j] = static_cast<double>(i + j);
                matrix2[i][j] = static_cast<double>(i * j);
            }
        }
        
        // Perform multiplication
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                result[i][j] = 0.0;
                for (int k = 0; k < size; ++k) {
                    result[i][j] += matrix1[i][k] * matrix2[k][j];
                }
            }
        }
        
        cpuCounter->Stop();
        
        LT_LOG_INFO("CPU intensive operation: {:.2f}ms", cpuCounter->GetLastValue());
    }
    
    void TestMemoryAllocations() {
        LT_LOG_INFO("Testing Memory Allocations...");
        
        auto* allocCounter = m_monitor.CreateCounter("MemoryAllocations");
        
        allocCounter->Start();
        
        // Perform various memory allocations
        std::vector<std::unique_ptr<int[]>> allocations;
        
        for (int i = 0; i < 1000; ++i) {
            size_t size = (i % 100) + 1; // 1 to 100 elements
            allocations.emplace_back(std::make_unique<int[]>(size));
            
            // Fill with data
            for (size_t j = 0; j < size; ++j) {
                allocations.back()[j] = static_cast<int>(i + j);
            }
        }
        
        // Deallocate some randomly
        std::uniform_int_distribution<> dis(0, allocations.size() - 1);
        for (int i = 0; i < 500; ++i) {
            int index = dis(m_gen);
            if (index < allocations.size()) {
                allocations.erase(allocations.begin() + index);
            }
        }
        
        allocCounter->Stop();
        
        LT_LOG_INFO("Memory allocation test: {:.2f}ms", allocCounter->GetLastValue());
    }
    
    void OnMetricsCollected(const PerformanceMetrics& metrics) {
        LT_LOG_INFO("=== Metrics Callback ===");
        LT_LOG_INFO("Frame: {} ({} FPS avg)", metrics.frameCount, metrics.fpsAvg);
        LT_LOG_INFO("Frame Time: {:.2f}ms avg", metrics.frameTimeAvg);
        LT_LOG_INFO("Memory: {:.2f}MB current, {:.2f}MB peak", 
                   metrics.currentMemory / (1024.0 * 1024.0),
                   metrics.peakMemory / (1024.0 * 1024.0));
        LT_LOG_INFO("CPU: {:.1f}% usage ({:.1f}% avg)", metrics.cpuUsage, metrics.cpuUsageAvg);
        
        // Check for performance issues
        if (metrics.fpsAvg < 30.0) {
            LT_LOG_WARN("Low average FPS detected: {}", metrics.fpsAvg);
        }
        
        if (metrics.cpuUsage > 80.0) {
            LT_LOG_WARN("High CPU usage detected: {}%", metrics.cpuUsage);
        }
        
        if (metrics.currentMemory > 100 * 1024 * 1024) { // 100MB
            LT_LOG_WARN("High memory usage detected: {:.2f}MB", 
                       metrics.currentMemory / (1024.0 * 1024.0));
        }
    }
};

int main() {
    // Initialize platform detection
    Limitless::PlatformDetection::Initialize();
    
    LT_LOG_INFO("Starting Performance Monitor Test");
    LT_LOG_INFO("Platform: {}", Limitless::PlatformDetection::GetPlatformString());
    LT_LOG_INFO("Architecture: {}", Limitless::PlatformDetection::GetArchitectureString());
    LT_LOG_INFO("Compiler: {}", Limitless::PlatformDetection::GetCompilerString());
    
    try {
        PerformanceTest test;
        test.RunTest();
    } catch (const std::exception& e) {
        LT_LOG_ERROR("Test failed with exception: {}", e.what());
        return 1;
    }
    
    LT_LOG_INFO("Performance Monitor Test completed successfully");
    return 0;
} 