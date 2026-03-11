#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

namespace Limitless {

    // Forward declarations
    class PerformanceCounter;
    class PerformanceTimer;
    class MemoryTracker;
    
    // Platform-specific forward declarations
    class ICPUPlatform;
    class IGPUPlatform;
    class ISystemPlatform;

    /// Performance data structure containing various metrics.
    struct PerformanceMetrics {
        // Frame timing
        double frameTime = 0.0;           // Current frame time in milliseconds
        double frameTimeAvg = 0.0;        // Average frame time over the last N frames
        double fps = 0.0;                 // Current FPS
        double fpsAvg = 0.0;              // Average FPS over the last N frames
        
        // Memory usage
        uint64_t totalMemory = 0;       // Total allocated memory in bytes
        uint64_t peakMemory = 0;        // Peak memory usage in bytes
        uint64_t currentMemory = 0;     // Current memory usage in bytes
        uint32_t allocationCount = 0;   // Number of active allocations
        
        // CPU usage
        double cpuUsage = 0.0;            // CPU usage percentage
        double cpuUsageAvg = 0.0;         // Average CPU usage over time
        uint32_t cpuCoreCount = 0;      // Number of CPU cores
        
        // GPU metrics (if available)
        double gpuUsage = 0.0;            // GPU usage percentage
        double gpuMemoryUsage = 0.0;      // GPU memory usage percentage
        double gpuTemperature = 0.0;      // GPU temperature in Celsius
        uint64_t gpuMemoryUsedBytes = 0;  // VRAM used (bytes)
        uint64_t gpuMemoryTotalBytes = 0; // VRAM total (bytes)
        
        // Performance counters
        std::unordered_map<std::string, double> counters;
        
        // System information
        uint64_t timestamp = 0;         // Timestamp when metrics were collected
        uint32_t frameCount = 0;        // Total frame count since start
    };

    /// Performance counter for tracking specific metrics.
    class PerformanceCounter {
    public:
        PerformanceCounter(const std::string& name);
        ~PerformanceCounter() = default;

        void Start();
        void Stop();
        void Reset();
        
        double GetLastValue() const;
        double GetAverageValue() const;
        double GetMinValue() const;
        double GetMaxValue() const;
        uint64_t GetSampleCount() const;
        
        const std::string& GetName() const { return m_Name; }

    private:
        std::string m_Name;
        std::chrono::high_resolution_clock::time_point m_StartTime;
        double m_LastValue;
        double m_TotalValue;
        double m_MinValue;
        double m_MaxValue;
        uint64_t m_SampleCount;
        bool m_IsRunning;
        mutable std::mutex m_Mutex;
    };

    /// High-resolution timer for precise measurements.
    class PerformanceTimer {
    public:
        PerformanceTimer();
        
        void Start();
        void Stop();
        void Reset();
        
        double GetElapsedMilliseconds() const;
        double GetElapsedMicroseconds() const;
        double GetElapsedNanoseconds() const;
        
        bool IsRunning() const { return m_IsRunning; }

    private:
        std::chrono::high_resolution_clock::time_point m_StartTime;
        std::chrono::high_resolution_clock::time_point m_EndTime;
        bool m_IsRunning;
    };

    /// Memory usage tracker.
    class MemoryTracker {
    public:
        MemoryTracker();
        ~MemoryTracker() = default;

        void TrackAllocation(size_t size);
        void TrackDeallocation(size_t size);
        void Reset();
        
        uint64_t GetCurrentMemory() const;
        uint64_t GetPeakMemory() const;
        uint64_t GetTotalMemory() const;
        uint32_t GetAllocationCount() const;
        
        void UpdateSystemMemory();

    private:
        std::atomic<uint64_t> m_CurrentMemory;
        std::atomic<uint64_t> m_PeakMemory;
        std::atomic<uint64_t> m_TotalMemory;
        std::atomic<uint32_t> m_AllocationCount;
        std::mutex m_Mutex;
    };

    /// CPU usage monitor.
    class CPUMonitor {
    public:
        CPUMonitor();
        ~CPUMonitor() = default;

        void Update();
        void Reset();
        
        double GetCurrentUsage() const;
        double GetAverageUsage() const;
        uint32_t GetCoreCount() const;
        
        void SetUpdateInterval(double intervalSeconds);

    private:
        double m_CurrentUsage;
        double m_AverageUsage;
        uint32_t m_CoreCount;
        double m_UpdateInterval;
        std::chrono::high_resolution_clock::time_point m_LastUpdate;
        
        // Platform-specific implementation
        std::unique_ptr<ICPUPlatform> m_Platform;
    };

    /// GPU metrics monitor.
    class GPUMonitor {
    public:
        GPUMonitor();
        ~GPUMonitor() = default;

        void Update();
        void Reset();
        
        double GetUsage() const;
        double GetMemoryUsage() const;
        double GetTemperature() const;
        bool IsAvailable() const;
        
        void SetUpdateInterval(double intervalSeconds);

    private:
        double m_Usage;
        double m_MemoryUsage;
        double m_Temperature;
        bool m_IsAvailable;
        double m_UpdateInterval;
        std::chrono::high_resolution_clock::time_point m_LastUpdate;
        
        // Platform-specific implementation
        std::unique_ptr<IGPUPlatform> m_Platform;
    };

    /// Main performance monitoring system.
    class PerformanceMonitor {
    public:
        static PerformanceMonitor& GetInstance();
        
        // Initialization and shutdown
        void Initialize();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }
        
        // Frame timing
        void BeginFrame();
        void EndFrame();
        double GetFrameTime() const;
        double GetAverageFrameTime() const;
        double GetFPS() const;
        double GetAverageFPS() const;
        uint32_t GetFrameCount() const;
        
        // Performance counters
        PerformanceCounter* CreateCounter(const std::string& name);
        PerformanceCounter* GetCounter(const std::string& name);
        void RemoveCounter(const std::string& name);
        void ResetAllCounters();
        
        // Memory tracking
        void TrackMemoryAllocation(size_t size);
        void TrackMemoryDeallocation(size_t size);
        MemoryTracker* GetMemoryTracker() { return &m_MemoryTracker; }
        
        // Metrics collection
        PerformanceMetrics CollectMetrics();
        void SetMetricsCollectionInterval(double intervalSeconds);
        
        // Callbacks
        using MetricsCallback = std::function<void(const PerformanceMetrics&)>;
        void SetMetricsCallback(MetricsCallback callback);
        
        // Configuration
        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }
        
        void SetLoggingEnabled(bool enabled) { m_LoggingEnabled = enabled; }
        bool IsLoggingEnabled() const { return m_LoggingEnabled; }
        
        // Utility methods
        void LogMetrics();
        std::string GetMetricsString() const;
        void SaveMetricsToFile(const std::string& filename);

    private:
        PerformanceMonitor() = default;
        ~PerformanceMonitor() = default;
        PerformanceMonitor(const PerformanceMonitor&) = delete;
        PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

        void UpdateMetrics();
        void LogFrameMetrics();
        PerformanceMetrics CollectMetricsInternal();
        
        // Non-locking internal methods for use when already locked
        double GetFrameTimeInternal() const;
        double GetAverageFrameTimeInternal() const;
        double GetFPSInternal() const;
        double GetAverageFPSInternal() const;

        bool m_Initialized = false;
        bool m_Enabled = false;
        bool m_LoggingEnabled = false;
        
        // Frame timing
        PerformanceTimer m_FrameTimer;
        std::vector<double> m_FrameTimes;
        size_t m_FrameTimeIndex = 0;
        uint32_t m_FrameCount = 0;
        std::chrono::high_resolution_clock::time_point m_LastFrameTime{};
        
        // Performance counters
        std::unordered_map<std::string, std::unique_ptr<PerformanceCounter>> m_Counters;
        std::mutex m_CountersMutex;
        
        // Memory tracking
        MemoryTracker m_MemoryTracker;
        
        // Platform-specific monitoring
        std::unique_ptr<ICPUPlatform> m_CpuPlatform;
        std::unique_ptr<IGPUPlatform> m_GpuPlatform;
        std::unique_ptr<ISystemPlatform> m_SystemPlatform;
        
        // Metrics collection
        PerformanceMetrics m_CurrentMetrics{};
        double m_MetricsCollectionInterval = 1.0;
        std::chrono::high_resolution_clock::time_point m_LastMetricsUpdate{};
        MetricsCallback m_MetricsCallback{};
        
        // Thread safety
        mutable std::mutex m_Mutex;
    };

    // Convenience macros for performance monitoring
    #define LT_PERF_BEGIN_FRAME() Limitless::PerformanceMonitor::GetInstance().BeginFrame()
    #define LT_PERF_END_FRAME() Limitless::PerformanceMonitor::GetInstance().EndFrame()
    
    #define LT_PERF_COUNTER(name) \
        auto* perfCounter = Limitless::PerformanceMonitor::GetInstance().GetCounter(name); \
        if (perfCounter) perfCounter->Start(); \
        auto perfCleanup = [perfCounter](void*) { if (perfCounter) perfCounter->Stop(); }; \
        std::unique_ptr<void, decltype(perfCleanup)> perfGuard(nullptr, perfCleanup)
    
    #define LT_PERF_SCOPE(name) \
        Limitless::PerformanceTimer perfTimer; \
        perfTimer.Start(); \
        auto perfScopeCleanup = [&perfTimer]() { perfTimer.Stop(); }; \
        std::unique_ptr<void, decltype(perfScopeCleanup)> perfScopeGuard(nullptr, perfScopeCleanup)
    
    #define LT_PERF_TRACK_MEMORY(size) Limitless::PerformanceMonitor::GetInstance().TrackMemoryAllocation(size)
    #define LT_PERF_UNTrack_MEMORY(size) Limitless::PerformanceMonitor::GetInstance().TrackMemoryDeallocation(size)

} // namespace Limitless 