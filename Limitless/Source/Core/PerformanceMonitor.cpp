#include "PerformanceMonitor.h"
#include "GPUMetricsProvider.h"
#include "Debug/Log.h"
#include "Platform/Platform.h"
#include "Platform/PerformancePlatform.h"
#include "Error.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Limitless {

    // PerformanceCounter Implementation
    PerformanceCounter::PerformanceCounter(const std::string& name)
        : m_Name(name), m_LastValue(0.0), m_TotalValue(0.0), m_MinValue(0.0), m_MaxValue(0.0), m_SampleCount(0), m_IsRunning(false) {
    }

    void PerformanceCounter::Start() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_IsRunning) {
            m_StartTime = std::chrono::high_resolution_clock::now();
            m_IsRunning = true;
        }
    }

    void PerformanceCounter::Stop() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_IsRunning) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
            m_LastValue = duration.count() / 1000.0; // Convert to milliseconds
            
            m_TotalValue += m_LastValue;
            m_SampleCount++;
            
            if (m_SampleCount == 1) {
                m_MinValue = m_LastValue;
                m_MaxValue = m_LastValue;
            } else {
                m_MinValue = std::min(m_MinValue, m_LastValue);
                m_MaxValue = std::max(m_MaxValue, m_LastValue);
            }
            
            m_IsRunning = false;
        }
    }

    void PerformanceCounter::Reset() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_LastValue = 0.0;
        m_TotalValue = 0.0;
        m_MinValue = 0.0;
        m_MaxValue = 0.0;
        m_SampleCount = 0;
        m_IsRunning = false;
    }

    double PerformanceCounter::GetLastValue() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_LastValue;
    }

    double PerformanceCounter::GetAverageValue() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_SampleCount > 0 ? m_TotalValue / m_SampleCount : 0.0;
    }

    double PerformanceCounter::GetMinValue() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_MinValue;
    }

    double PerformanceCounter::GetMaxValue() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_MaxValue;
    }

    uint64_t PerformanceCounter::GetSampleCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_SampleCount;
    }

    // PerformanceTimer Implementation
    PerformanceTimer::PerformanceTimer() : m_IsRunning(false) {
    }

    void PerformanceTimer::Start() {
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_IsRunning = true;
    }

    void PerformanceTimer::Stop() {
        if (m_IsRunning) {
            m_EndTime = std::chrono::high_resolution_clock::now();
            m_IsRunning = false;
        }
    }

    void PerformanceTimer::Reset() {
        m_IsRunning = false;
    }

    double PerformanceTimer::GetElapsedMilliseconds() const {
        auto endTime = m_IsRunning ? std::chrono::high_resolution_clock::now() : m_EndTime;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
        return duration.count() / 1000.0;
    }

    double PerformanceTimer::GetElapsedMicroseconds() const {
        auto endTime = m_IsRunning ? std::chrono::high_resolution_clock::now() : m_EndTime;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
        return static_cast<double>(duration.count());
    }

    double PerformanceTimer::GetElapsedNanoseconds() const {
        auto endTime = m_IsRunning ? std::chrono::high_resolution_clock::now() : m_EndTime;
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - m_StartTime);
        return static_cast<double>(duration.count());
    }

    // MemoryTracker Implementation
    MemoryTracker::MemoryTracker() 
        : m_CurrentMemory(0), m_PeakMemory(0), m_TotalMemory(0), m_AllocationCount(0) {
    }

    void MemoryTracker::TrackAllocation(size_t size) {
        m_CurrentMemory.fetch_add(size);
        m_TotalMemory.fetch_add(size);
        m_AllocationCount.fetch_add(1);
        
        uint64_t current = m_CurrentMemory.load();
        uint64_t peak = m_PeakMemory.load();
        while (current > peak && !m_PeakMemory.compare_exchange_weak(peak, current)) {
            // Retry if peak was updated by another thread
        }
    }

    void MemoryTracker::TrackDeallocation(size_t size) {
        m_CurrentMemory.fetch_sub(size);
        m_AllocationCount.fetch_sub(1);
    }

    void MemoryTracker::Reset() {
        m_CurrentMemory.store(0);
        m_PeakMemory.store(0);
        m_TotalMemory.store(0);
        m_AllocationCount.store(0);
    }

    uint64_t MemoryTracker::GetCurrentMemory() const {
        return m_CurrentMemory.load();
    }

    uint64_t MemoryTracker::GetPeakMemory() const {
        return m_PeakMemory.load();
    }

    uint64_t MemoryTracker::GetTotalMemory() const {
        return m_TotalMemory.load();
    }

    uint32_t MemoryTracker::GetAllocationCount() const {
        return m_AllocationCount.load();
    }

    void MemoryTracker::UpdateSystemMemory() {
        // This could be extended to get actual system memory usage
        // For now, we rely on our own tracking
    }

    // CPUMonitor Implementation - Now uses platform abstraction
    CPUMonitor::CPUMonitor() 
        : m_CurrentUsage(0.0), m_AverageUsage(0.0), m_CoreCount(0), m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
        
        // Create platform-specific CPU monitor
        m_Platform = PerformancePlatformFactory::CreateCPUPlatform();
        if (m_Platform) {
            m_Platform->Initialize();
            m_CoreCount = m_Platform->GetCoreCount();
        }
    }

    void CPUMonitor::Update() {
        if (!m_Platform) {
            return;
        }

        m_Platform->Update();
        m_CurrentUsage = m_Platform->GetCurrentUsage();
        m_AverageUsage = m_Platform->GetAverageUsage();
    }

    void CPUMonitor::Reset() {
        if (m_Platform) {
            m_Platform->Reset();
        }
        m_CurrentUsage = 0.0;
        m_AverageUsage = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double CPUMonitor::GetCurrentUsage() const {
        return m_CurrentUsage;
    }

    double CPUMonitor::GetAverageUsage() const {
        return m_AverageUsage;
    }

    uint32_t CPUMonitor::GetCoreCount() const {
        return m_CoreCount;
    }

    void CPUMonitor::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
        if (m_Platform) {
            m_Platform->SetUpdateInterval(intervalSeconds);
        }
    }

    // GPUMonitor Implementation - Now uses platform abstraction
    GPUMonitor::GPUMonitor() 
        : m_Usage(0.0), m_MemoryUsage(0.0), m_Temperature(0.0), m_IsAvailable(false), m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
        
        // Create platform-specific GPU monitor
        m_Platform = PerformancePlatformFactory::CreateGPUPlatform();
        if (m_Platform) {
            m_Platform->Initialize();
            m_IsAvailable = m_Platform->IsAvailable();
        }
    }

    void GPUMonitor::Update() {
        if (!m_Platform || !m_IsAvailable) {
            return;
        }

        m_Platform->Update();
        m_Usage = m_Platform->GetUsage();
        m_MemoryUsage = m_Platform->GetMemoryUsage();
        m_Temperature = m_Platform->GetTemperature();
    }

    void GPUMonitor::Reset() {
        if (m_Platform) {
            m_Platform->Reset();
        }
        m_Usage = 0.0;
        m_MemoryUsage = 0.0;
        m_Temperature = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double GPUMonitor::GetUsage() const {
        return m_Usage;
    }

    double GPUMonitor::GetMemoryUsage() const {
        return m_MemoryUsage;
    }

    double GPUMonitor::GetTemperature() const {
        return m_Temperature;
    }

    bool GPUMonitor::IsAvailable() const {
        return m_IsAvailable;
    }

    void GPUMonitor::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
        if (m_Platform) {
            m_Platform->SetUpdateInterval(intervalSeconds);
        }
    }

    // PerformanceMonitor Implementation
    PerformanceMonitor& PerformanceMonitor::GetInstance() {
        static PerformanceMonitor instance;
        return instance;
    }

    void PerformanceMonitor::Initialize() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (m_Initialized) {
            return;
        }

        m_Initialized = true;
        m_Enabled = true;
        m_LoggingEnabled = false;
        m_FrameCount = 0;
        m_FrameTimeIndex = 0;
        m_MetricsCollectionInterval = 1.0; // 1 second default
        m_MetricsCallback = nullptr;
        m_LastMetricsUpdate = std::chrono::high_resolution_clock::now();
        
        // Initialize frame time history (keep last 60 frames)
        m_FrameTimes.resize(60, 0.0);
        
        // Initialize platform-specific monitors
        m_CpuPlatform = PerformancePlatformFactory::CreateCPUPlatform();
        m_GpuPlatform = PerformancePlatformFactory::CreateGPUPlatform();
        m_SystemPlatform = PerformancePlatformFactory::CreateSystemPlatform();
        
        if (m_CpuPlatform) {
            m_CpuPlatform->Initialize();
            m_CpuPlatform->SetUpdateInterval(0.5); // Update every 500ms
        }
        
        if (m_GpuPlatform) {
            m_GpuPlatform->Initialize();
            m_GpuPlatform->SetUpdateInterval(1.0); // Update every 1 second
        }
        
        if (m_SystemPlatform) {
            m_SystemPlatform->Initialize();
        }
        
        LT_CORE_INFO("Performance Monitor initialized");
    }

    void PerformanceMonitor::Shutdown() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        if (!m_Initialized) {
            return;
        }

        // Log final metrics
        if (m_LoggingEnabled) {
            LogMetrics();
        }

        // Clear callback to avoid cross-test / cross-run dangling captures.
        m_MetricsCallback = nullptr;

        // Clear counters and current metrics to reset state cleanly.
        {
            std::lock_guard<std::mutex> countersLock(m_CountersMutex);
            m_Counters.clear();
        }
        m_CurrentMetrics = PerformanceMetrics{};

        m_Initialized = false;
        m_Enabled = false;
        
        LT_CORE_INFO("Performance Monitor shutdown");
    }

    void PerformanceMonitor::BeginFrame() {
        if (!m_Enabled || !m_Initialized) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FrameTimer.Start();
        m_LastFrameTime = std::chrono::high_resolution_clock::now();
    }

    void PerformanceMonitor::EndFrame() {
        if (!m_Enabled || !m_Initialized) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        
        m_FrameTimer.Stop();
        double frameTime = m_FrameTimer.GetElapsedMilliseconds();
        
        // Update frame time history
        m_FrameTimes[m_FrameTimeIndex] = frameTime;
        m_FrameTimeIndex = (m_FrameTimeIndex + 1) % m_FrameTimes.size();
        m_FrameCount++;
        
        // Update metrics periodically
        UpdateMetrics();
        
        // Log frame metrics if enabled
        if (m_LoggingEnabled) {
            LogFrameMetrics();
        }
    }

    double PerformanceMonitor::GetFrameTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_FrameTimer.GetElapsedMilliseconds();
    }

    double PerformanceMonitor::GetFrameTimeInternal() const {
        return m_FrameTimer.GetElapsedMilliseconds();
    }

    double PerformanceMonitor::GetAverageFrameTime() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return GetAverageFrameTimeInternal();
    }

    double PerformanceMonitor::GetAverageFrameTimeInternal() const {
        if (m_FrameTimes.empty()) {
            return 0.0;
        }
        
        double sum = 0.0;
        size_t count = 0;
        
        for (double frameTime : m_FrameTimes) {
            if (frameTime > 0.0) {
                sum += frameTime;
                count++;
            }
        }
        
        return count > 0 ? sum / count : 0.0;
    }

    double PerformanceMonitor::GetFPS() const {
        double frameTime = GetFrameTime();
        return frameTime > 0.0 ? 1000.0 / frameTime : 0.0;
    }

    double PerformanceMonitor::GetFPSInternal() const {
        double frameTime = GetFrameTimeInternal();
        return frameTime > 0.0 ? 1000.0 / frameTime : 0.0;
    }

    double PerformanceMonitor::GetAverageFPS() const {
        double avgFrameTime = GetAverageFrameTime();
        return avgFrameTime > 0.0 ? 1000.0 / avgFrameTime : 0.0;
    }

    double PerformanceMonitor::GetAverageFPSInternal() const {
        double avgFrameTime = GetAverageFrameTimeInternal();
        return avgFrameTime > 0.0 ? 1000.0 / avgFrameTime : 0.0;
    }

    uint32_t PerformanceMonitor::GetFrameCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_FrameCount;
    }

    PerformanceCounter* PerformanceMonitor::CreateCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_CountersMutex);
        
        auto it = m_Counters.find(name);
        if (it != m_Counters.end()) {
            return it->second.get();
        }
        
        auto counter = std::make_unique<PerformanceCounter>(name);
        PerformanceCounter* ptr = counter.get();
        m_Counters[name] = std::move(counter);
        
        return ptr;
    }

    PerformanceCounter* PerformanceMonitor::GetCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_CountersMutex);
        
        auto it = m_Counters.find(name);
        return it != m_Counters.end() ? it->second.get() : nullptr;
    }

    void PerformanceMonitor::RemoveCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_CountersMutex);
        m_Counters.erase(name);
    }

    void PerformanceMonitor::ResetAllCounters() {
        std::lock_guard<std::mutex> lock(m_CountersMutex);
        for (auto& pair : m_Counters) {
            pair.second->Reset();
        }
    }

    void PerformanceMonitor::TrackMemoryAllocation(size_t size) {
        if (m_Enabled) {
            m_MemoryTracker.TrackAllocation(size);
        }
    }

    void PerformanceMonitor::TrackMemoryDeallocation(size_t size) {
        if (m_Enabled) {
            m_MemoryTracker.TrackDeallocation(size);
        }
    }

    PerformanceMetrics PerformanceMonitor::CollectMetricsInternal() {
        // Ensure we're initialized
        if (!m_Initialized) {
            // Return empty metrics if not initialized
            PerformanceMetrics emptyMetrics = {};
            return emptyMetrics;
        }
        
        // Update platform-specific monitors
        if (m_CpuPlatform) {
            m_CpuPlatform->Update();
        }
        if (m_GpuPlatform) {
            m_GpuPlatform->Update();
        }
        if (m_SystemPlatform) {
            m_SystemPlatform->Update();
        }
        
        // Collect frame timing data
        m_CurrentMetrics.frameTime = GetFrameTimeInternal();
        m_CurrentMetrics.frameTimeAvg = GetAverageFrameTimeInternal();
        m_CurrentMetrics.fps = GetFPSInternal();
        m_CurrentMetrics.fpsAvg = GetAverageFPSInternal();
        m_CurrentMetrics.frameCount = m_FrameCount;
        
        // Collect memory data
        m_CurrentMetrics.currentMemory = m_MemoryTracker.GetCurrentMemory();
        m_CurrentMetrics.peakMemory = m_MemoryTracker.GetPeakMemory();
        m_CurrentMetrics.totalMemory = m_MemoryTracker.GetTotalMemory();
        m_CurrentMetrics.allocationCount = m_MemoryTracker.GetAllocationCount();
        
        // Collect CPU data
        if (m_CpuPlatform) {
            m_CurrentMetrics.cpuUsage = m_CpuPlatform->GetCurrentUsage();
            m_CurrentMetrics.cpuUsageAvg = m_CpuPlatform->GetAverageUsage();
            m_CurrentMetrics.cpuCoreCount = m_CpuPlatform->GetCoreCount();
        } else {
            m_CurrentMetrics.cpuUsage = 0.0;
            m_CurrentMetrics.cpuUsageAvg = 0.0;
            m_CurrentMetrics.cpuCoreCount = 0;
        }
        
        // Collect GPU data
        if (m_GpuPlatform) {
            m_CurrentMetrics.gpuUsage = m_GpuPlatform->GetUsage();
            m_CurrentMetrics.gpuMemoryUsage = m_GpuPlatform->GetMemoryUsage();
            m_CurrentMetrics.gpuTemperature = m_GpuPlatform->GetTemperature();
        } else {
            m_CurrentMetrics.gpuUsage = 0.0;
            m_CurrentMetrics.gpuMemoryUsage = 0.0;
            m_CurrentMetrics.gpuTemperature = 0.0;
        }
        {
            uint64_t usedB = 0, totalB = 0;
            GPUMetricsProvider::GetVram(usedB, totalB);
            m_CurrentMetrics.gpuMemoryUsedBytes = usedB;
            m_CurrentMetrics.gpuMemoryTotalBytes = totalB;
        }
        
        // Collect performance counter data
        m_CurrentMetrics.counters.clear();
        {
            std::lock_guard<std::mutex> countersLock(m_CountersMutex);
            for (const auto& pair : m_Counters) {
                m_CurrentMetrics.counters[pair.first] = pair.second->GetLastValue();
            }
        }
        
        // Set timestamp
        m_CurrentMetrics.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        return m_CurrentMetrics;
    }

    PerformanceMetrics PerformanceMonitor::CollectMetrics() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return CollectMetricsInternal();
    }

    void PerformanceMonitor::SetMetricsCollectionInterval(double intervalSeconds) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MetricsCollectionInterval = intervalSeconds;
    }

    void PerformanceMonitor::SetMetricsCallback(MetricsCallback callback) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_MetricsCallback = callback;
    }

    void PerformanceMonitor::UpdateMetrics() {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_LastMetricsUpdate).count();
        
        if (elapsed >= m_MetricsCollectionInterval) {
            CollectMetricsInternal();
            m_LastMetricsUpdate = now;
            
            // Call callback if set
            if (m_MetricsCallback) {
                m_MetricsCallback(m_CurrentMetrics);
            }
        }
    }

    void PerformanceMonitor::LogFrameMetrics() {
        if (!m_LoggingEnabled) {
            return;
        }

        double frameTime = GetFrameTimeInternal();
        double fps = GetFPSInternal();
        
        LT_CORE_DEBUG("Frame {}: {:.2f}ms ({:.1f} FPS)", m_FrameCount, frameTime, fps);
    }

    void PerformanceMonitor::LogMetrics() {
        if (!m_LoggingEnabled) {
            return;
        }

        auto metrics = CollectMetricsInternal();
        
        LT_CORE_INFO("=== Performance Metrics ===");
        LT_CORE_INFO("Frame: {} ({} FPS avg)", metrics.frameCount, metrics.fpsAvg);
        LT_CORE_INFO("Frame Time: {:.2f}ms avg", metrics.frameTimeAvg);
        LT_CORE_INFO("Memory: {:.2f}MB current, {:.2f}MB peak", 
                   metrics.currentMemory / (1024.0 * 1024.0),
                   metrics.peakMemory / (1024.0 * 1024.0));
        LT_CORE_INFO("CPU: {:.1f}% usage ({:.1f}% avg)", metrics.cpuUsage, metrics.cpuUsageAvg);
        
        if (metrics.gpuUsage > 0.0) {
            LT_CORE_INFO("GPU: {:.1f}% usage, {:.1f}% memory", metrics.gpuUsage, metrics.gpuMemoryUsage);
        }
        
        if (!metrics.counters.empty()) {
            LT_CORE_INFO("Counters:");
            for (const auto& pair : metrics.counters) {
                LT_CORE_INFO("  {}: {:.2f}ms", pair.first, pair.second);
            }
        }
    }

    std::string PerformanceMonitor::GetMetricsString() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        // Use safe access to metrics with fallback values
        uint32_t frameCount = m_Initialized ? m_CurrentMetrics.frameCount : 0;
        double fpsAvg = m_Initialized ? m_CurrentMetrics.fpsAvg : 0.0;
        double frameTimeAvg = m_Initialized ? m_CurrentMetrics.frameTimeAvg : 0.0;
        uint64_t currentMemory = m_Initialized ? m_CurrentMetrics.currentMemory : 0;
        uint64_t peakMemory = m_Initialized ? m_CurrentMetrics.peakMemory : 0;
        double cpuUsage = m_Initialized ? m_CurrentMetrics.cpuUsage : 0.0;
        double cpuUsageAvg = m_Initialized ? m_CurrentMetrics.cpuUsageAvg : 0.0;
        double gpuUsage = m_Initialized ? m_CurrentMetrics.gpuUsage : 0.0;
        double gpuMemoryUsage = m_Initialized ? m_CurrentMetrics.gpuMemoryUsage : 0.0;
        
        oss << "Frame: " << frameCount 
            << " (" << fpsAvg << " FPS avg)\n";
        oss << "Frame Time: " << frameTimeAvg << "ms avg\n";
        oss << "Memory: " << (currentMemory / (1024.0 * 1024.0)) << "MB current, "
            << (peakMemory / (1024.0 * 1024.0)) << "MB peak\n";
        oss << "CPU: " << cpuUsage << "% usage (" 
            << cpuUsageAvg << "% avg)\n";
        
        if (gpuUsage > 0.0) {
            oss << "GPU: " << gpuUsage << "% usage, "
                << gpuMemoryUsage << "% memory\n";
        }
        
        return oss.str();
    }

    void PerformanceMonitor::SaveMetricsToFile(const std::string& filename) {
        auto metrics = CollectMetrics();
        
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "Performance Metrics Report\n";
            file << "==========================\n\n";
            file << "Timestamp: " << metrics.timestamp << "\n";
            file << "Frame Count: " << metrics.frameCount << "\n";
            file << "Average FPS: " << metrics.fpsAvg << "\n";
            file << "Average Frame Time: " << metrics.frameTimeAvg << "ms\n";
            file << "Current Memory: " << (metrics.currentMemory / (1024.0 * 1024.0)) << "MB\n";
            file << "Peak Memory: " << (metrics.peakMemory / (1024.0 * 1024.0)) << "MB\n";
            file << "CPU Usage: " << metrics.cpuUsage << "%\n";
            file << "CPU Usage Average: " << metrics.cpuUsageAvg << "%\n";
            file << "CPU Cores: " << metrics.cpuCoreCount << "\n";
            
            if (metrics.gpuUsage > 0.0) {
                file << "GPU Usage: " << metrics.gpuUsage << "%\n";
                file << "GPU Memory Usage: " << metrics.gpuMemoryUsage << "%\n";
                file << "GPU Temperature: " << metrics.gpuTemperature << "°C\n";
            }
            
            if (!metrics.counters.empty()) {
                file << "\nPerformance Counters:\n";
                for (const auto& pair : metrics.counters) {
                    file << "  " << pair.first << ": " << pair.second << "ms\n";
                }
            }
            
            file.close();
        }
    }

} // namespace Limitless 