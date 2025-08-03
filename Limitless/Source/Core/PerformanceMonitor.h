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
    class CPUMonitor;
    class GPUMonitor;

    /**
     * @brief Performance data structure containing various metrics
     */
    struct PerformanceMetrics {
        // Frame timing
        double frameTime;           // Current frame time in milliseconds
        double frameTimeAvg;        // Average frame time over the last N frames
        double fps;                 // Current FPS
        double fpsAvg;              // Average FPS over the last N frames
        
        // Memory usage
        uint64_t totalMemory;       // Total allocated memory in bytes
        uint64_t peakMemory;        // Peak memory usage in bytes
        uint64_t currentMemory;     // Current memory usage in bytes
        uint32_t allocationCount;   // Number of active allocations
        
        // CPU usage
        double cpuUsage;            // CPU usage percentage
        double cpuUsageAvg;         // Average CPU usage over time
        uint32_t cpuCoreCount;      // Number of CPU cores
        
        // GPU metrics (if available)
        double gpuUsage;            // GPU usage percentage
        double gpuMemoryUsage;      // GPU memory usage percentage
        double gpuTemperature;      // GPU temperature in Celsius
        
        // Performance counters
        std::unordered_map<std::string, double> counters;
        
        // System information
        uint64_t timestamp;         // Timestamp when metrics were collected
        uint32_t frameCount;        // Total frame count since start
    };

    /**
     * @brief Performance counter for tracking specific metrics
     */
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
        
        const std::string& GetName() const { return m_name; }

    private:
        std::string m_name;
        std::chrono::high_resolution_clock::time_point m_startTime;
        double m_lastValue;
        double m_totalValue;
        double m_minValue;
        double m_maxValue;
        uint64_t m_sampleCount;
        bool m_isRunning;
        mutable std::mutex m_mutex;
    };

    /**
     * @brief High-resolution timer for precise measurements
     */
    class PerformanceTimer {
    public:
        PerformanceTimer();
        
        void Start();
        void Stop();
        void Reset();
        
        double GetElapsedMilliseconds() const;
        double GetElapsedMicroseconds() const;
        double GetElapsedNanoseconds() const;
        
        bool IsRunning() const { return m_isRunning; }

    private:
        std::chrono::high_resolution_clock::time_point m_startTime;
        std::chrono::high_resolution_clock::time_point m_endTime;
        bool m_isRunning;
    };

    /**
     * @brief Memory usage tracker
     */
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
        std::atomic<uint64_t> m_currentMemory;
        std::atomic<uint64_t> m_peakMemory;
        std::atomic<uint64_t> m_totalMemory;
        std::atomic<uint32_t> m_allocationCount;
        std::mutex m_mutex;
    };

    /**
     * @brief CPU usage monitor
     */
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
        double m_currentUsage;
        double m_averageUsage;
        uint32_t m_coreCount;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
        
        // Platform-specific data
        struct PlatformData;
        std::unique_ptr<PlatformData> m_platformData;
    };

    /**
     * @brief GPU metrics monitor
     */
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
        double m_usage;
        double m_memoryUsage;
        double m_temperature;
        bool m_isAvailable;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
        
        // Platform-specific data
        struct PlatformData;
        std::unique_ptr<PlatformData> m_platformData;
    };

    /**
     * @brief Main performance monitoring system
     */
    class PerformanceMonitor {
    public:
        static PerformanceMonitor& GetInstance();
        
        // Initialization and shutdown
        void Initialize();
        void Shutdown();
        bool IsInitialized() const { return m_initialized; }
        
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
        MemoryTracker* GetMemoryTracker() { return &m_memoryTracker; }
        
        // Metrics collection
        PerformanceMetrics CollectMetrics();
        void SetMetricsCollectionInterval(double intervalSeconds);
        
        // Callbacks
        using MetricsCallback = std::function<void(const PerformanceMetrics&)>;
        void SetMetricsCallback(MetricsCallback callback);
        
        // Configuration
        void SetEnabled(bool enabled) { m_enabled = enabled; }
        bool IsEnabled() const { return m_enabled; }
        
        void SetLoggingEnabled(bool enabled) { m_loggingEnabled = enabled; }
        bool IsLoggingEnabled() const { return m_loggingEnabled; }
        
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

        bool m_initialized;
        bool m_enabled;
        bool m_loggingEnabled;
        
        // Frame timing
        PerformanceTimer m_frameTimer;
        std::vector<double> m_frameTimes;
        size_t m_frameTimeIndex;
        uint32_t m_frameCount;
        std::chrono::high_resolution_clock::time_point m_lastFrameTime;
        
        // Performance counters
        std::unordered_map<std::string, std::unique_ptr<PerformanceCounter>> m_counters;
        std::mutex m_countersMutex;
        
        // Memory tracking
        MemoryTracker m_memoryTracker;
        
        // CPU and GPU monitoring
        CPUMonitor m_cpuMonitor;
        GPUMonitor m_gpuMonitor;
        
        // Metrics collection
        PerformanceMetrics m_currentMetrics;
        double m_metricsCollectionInterval;
        std::chrono::high_resolution_clock::time_point m_lastMetricsUpdate;
        MetricsCallback m_metricsCallback;
        
        // Thread safety
        mutable std::mutex m_mutex;
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