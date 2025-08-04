#include "PerformanceMonitor.h"
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
        : m_name(name), m_lastValue(0.0), m_totalValue(0.0), m_minValue(0.0), m_maxValue(0.0), m_sampleCount(0), m_isRunning(false) {
    }

    void PerformanceCounter::Start() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isRunning) {
            m_startTime = std::chrono::high_resolution_clock::now();
            m_isRunning = true;
        }
    }

    void PerformanceCounter::Stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_isRunning) {
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
            m_lastValue = duration.count() / 1000.0; // Convert to milliseconds
            
            m_totalValue += m_lastValue;
            m_sampleCount++;
            
            if (m_sampleCount == 1) {
                m_minValue = m_lastValue;
                m_maxValue = m_lastValue;
            } else {
                m_minValue = std::min(m_minValue, m_lastValue);
                m_maxValue = std::max(m_maxValue, m_lastValue);
            }
            
            m_isRunning = false;
        }
    }

    void PerformanceCounter::Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastValue = 0.0;
        m_totalValue = 0.0;
        m_minValue = 0.0;
        m_maxValue = 0.0;
        m_sampleCount = 0;
        m_isRunning = false;
    }

    double PerformanceCounter::GetLastValue() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastValue;
    }

    double PerformanceCounter::GetAverageValue() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sampleCount > 0 ? m_totalValue / m_sampleCount : 0.0;
    }

    double PerformanceCounter::GetMinValue() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_minValue;
    }

    double PerformanceCounter::GetMaxValue() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_maxValue;
    }

    uint64_t PerformanceCounter::GetSampleCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sampleCount;
    }

    // PerformanceTimer Implementation
    PerformanceTimer::PerformanceTimer() : m_isRunning(false) {
    }

    void PerformanceTimer::Start() {
        m_startTime = std::chrono::high_resolution_clock::now();
        m_isRunning = true;
    }

    void PerformanceTimer::Stop() {
        if (m_isRunning) {
            m_endTime = std::chrono::high_resolution_clock::now();
            m_isRunning = false;
        }
    }

    void PerformanceTimer::Reset() {
        m_isRunning = false;
    }

    double PerformanceTimer::GetElapsedMilliseconds() const {
        auto endTime = m_isRunning ? std::chrono::high_resolution_clock::now() : m_endTime;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
        return duration.count() / 1000.0;
    }

    double PerformanceTimer::GetElapsedMicroseconds() const {
        auto endTime = m_isRunning ? std::chrono::high_resolution_clock::now() : m_endTime;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
        return static_cast<double>(duration.count());
    }

    double PerformanceTimer::GetElapsedNanoseconds() const {
        auto endTime = m_isRunning ? std::chrono::high_resolution_clock::now() : m_endTime;
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - m_startTime);
        return static_cast<double>(duration.count());
    }

    // MemoryTracker Implementation
    MemoryTracker::MemoryTracker() 
        : m_currentMemory(0), m_peakMemory(0), m_totalMemory(0), m_allocationCount(0) {
    }

    void MemoryTracker::TrackAllocation(size_t size) {
        m_currentMemory.fetch_add(size);
        m_totalMemory.fetch_add(size);
        m_allocationCount.fetch_add(1);
        
        uint64_t current = m_currentMemory.load();
        uint64_t peak = m_peakMemory.load();
        while (current > peak && !m_peakMemory.compare_exchange_weak(peak, current)) {
            // Retry if peak was updated by another thread
        }
    }

    void MemoryTracker::TrackDeallocation(size_t size) {
        m_currentMemory.fetch_sub(size);
        m_allocationCount.fetch_sub(1);
    }

    void MemoryTracker::Reset() {
        m_currentMemory.store(0);
        m_peakMemory.store(0);
        m_totalMemory.store(0);
        m_allocationCount.store(0);
    }

    uint64_t MemoryTracker::GetCurrentMemory() const {
        return m_currentMemory.load();
    }

    uint64_t MemoryTracker::GetPeakMemory() const {
        return m_peakMemory.load();
    }

    uint64_t MemoryTracker::GetTotalMemory() const {
        return m_totalMemory.load();
    }

    uint32_t MemoryTracker::GetAllocationCount() const {
        return m_allocationCount.load();
    }

    void MemoryTracker::UpdateSystemMemory() {
        // This could be extended to get actual system memory usage
        // For now, we rely on our own tracking
    }

    // CPUMonitor Implementation - Now uses platform abstraction
    CPUMonitor::CPUMonitor() 
        : m_currentUsage(0.0), m_averageUsage(0.0), m_coreCount(0), m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
        
        // Create platform-specific CPU monitor
        m_platform = PerformancePlatformFactory::CreateCPUPlatform();
        if (m_platform) {
            m_platform->Initialize();
            m_coreCount = m_platform->GetCoreCount();
        }
    }

    void CPUMonitor::Update() {
        if (!m_platform) {
            return;
        }

        m_platform->Update();
        m_currentUsage = m_platform->GetCurrentUsage();
        m_averageUsage = m_platform->GetAverageUsage();
    }

    void CPUMonitor::Reset() {
        if (m_platform) {
            m_platform->Reset();
        }
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double CPUMonitor::GetCurrentUsage() const {
        return m_currentUsage;
    }

    double CPUMonitor::GetAverageUsage() const {
        return m_averageUsage;
    }

    uint32_t CPUMonitor::GetCoreCount() const {
        return m_coreCount;
    }

    void CPUMonitor::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
        if (m_platform) {
            m_platform->SetUpdateInterval(intervalSeconds);
        }
    }

    // GPUMonitor Implementation - Now uses platform abstraction
    GPUMonitor::GPUMonitor() 
        : m_usage(0.0), m_memoryUsage(0.0), m_temperature(0.0), m_isAvailable(false), m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
        
        // Create platform-specific GPU monitor
        m_platform = PerformancePlatformFactory::CreateGPUPlatform();
        if (m_platform) {
            m_platform->Initialize();
            m_isAvailable = m_platform->IsAvailable();
        }
    }

    void GPUMonitor::Update() {
        if (!m_platform || !m_isAvailable) {
            return;
        }

        m_platform->Update();
        m_usage = m_platform->GetUsage();
        m_memoryUsage = m_platform->GetMemoryUsage();
        m_temperature = m_platform->GetTemperature();
    }

    void GPUMonitor::Reset() {
        if (m_platform) {
            m_platform->Reset();
        }
        m_usage = 0.0;
        m_memoryUsage = 0.0;
        m_temperature = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double GPUMonitor::GetUsage() const {
        return m_usage;
    }

    double GPUMonitor::GetMemoryUsage() const {
        return m_memoryUsage;
    }

    double GPUMonitor::GetTemperature() const {
        return m_temperature;
    }

    bool GPUMonitor::IsAvailable() const {
        return m_isAvailable;
    }

    void GPUMonitor::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
        if (m_platform) {
            m_platform->SetUpdateInterval(intervalSeconds);
        }
    }

    // PerformanceMonitor Implementation
    PerformanceMonitor& PerformanceMonitor::GetInstance() {
        static PerformanceMonitor instance;
        return instance;
    }

    void PerformanceMonitor::Initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_initialized) {
            return;
        }

        m_initialized = true;
        m_enabled = true;
        m_loggingEnabled = false;
        m_frameCount = 0;
        m_frameTimeIndex = 0;
        m_metricsCollectionInterval = 1.0; // 1 second default
        
        // Initialize frame time history (keep last 60 frames)
        m_frameTimes.resize(60, 0.0);
        
        // Initialize platform-specific monitors
        m_cpuPlatform = PerformancePlatformFactory::CreateCPUPlatform();
        m_gpuPlatform = PerformancePlatformFactory::CreateGPUPlatform();
        m_systemPlatform = PerformancePlatformFactory::CreateSystemPlatform();
        
        if (m_cpuPlatform) {
            m_cpuPlatform->Initialize();
            m_cpuPlatform->SetUpdateInterval(0.5); // Update every 500ms
        }
        
        if (m_gpuPlatform) {
            m_gpuPlatform->Initialize();
            m_gpuPlatform->SetUpdateInterval(1.0); // Update every 1 second
        }
        
        if (m_systemPlatform) {
            m_systemPlatform->Initialize();
        }
        
        LT_CORE_INFO("Performance Monitor initialized");
    }

    void PerformanceMonitor::Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_initialized) {
            return;
        }

        // Log final metrics
        if (m_loggingEnabled) {
            LogMetrics();
        }

        m_initialized = false;
        m_enabled = false;
        
        LT_CORE_INFO("Performance Monitor shutdown");
    }

    void PerformanceMonitor::BeginFrame() {
        if (!m_enabled || !m_initialized) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimer.Start();
        m_lastFrameTime = std::chrono::high_resolution_clock::now();
    }

    void PerformanceMonitor::EndFrame() {
        if (!m_enabled || !m_initialized) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_frameTimer.Stop();
        double frameTime = m_frameTimer.GetElapsedMilliseconds();
        
        // Update frame time history
        m_frameTimes[m_frameTimeIndex] = frameTime;
        m_frameTimeIndex = (m_frameTimeIndex + 1) % m_frameTimes.size();
        m_frameCount++;
        
        // Update metrics periodically
        UpdateMetrics();
        
        // Log frame metrics if enabled
        if (m_loggingEnabled) {
            LogFrameMetrics();
        }
    }

    double PerformanceMonitor::GetFrameTime() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_frameTimer.GetElapsedMilliseconds();
    }

    double PerformanceMonitor::GetFrameTimeInternal() const {
        return m_frameTimer.GetElapsedMilliseconds();
    }

    double PerformanceMonitor::GetAverageFrameTime() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return GetAverageFrameTimeInternal();
    }

    double PerformanceMonitor::GetAverageFrameTimeInternal() const {
        if (m_frameTimes.empty()) {
            return 0.0;
        }
        
        double sum = 0.0;
        size_t count = 0;
        
        for (double frameTime : m_frameTimes) {
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
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_frameCount;
    }

    PerformanceCounter* PerformanceMonitor::CreateCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_countersMutex);
        
        auto it = m_counters.find(name);
        if (it != m_counters.end()) {
            return it->second.get();
        }
        
        auto counter = std::make_unique<PerformanceCounter>(name);
        PerformanceCounter* ptr = counter.get();
        m_counters[name] = std::move(counter);
        
        return ptr;
    }

    PerformanceCounter* PerformanceMonitor::GetCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_countersMutex);
        
        auto it = m_counters.find(name);
        return it != m_counters.end() ? it->second.get() : nullptr;
    }

    void PerformanceMonitor::RemoveCounter(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_countersMutex);
        m_counters.erase(name);
    }

    void PerformanceMonitor::ResetAllCounters() {
        std::lock_guard<std::mutex> lock(m_countersMutex);
        for (auto& pair : m_counters) {
            pair.second->Reset();
        }
    }

    void PerformanceMonitor::TrackMemoryAllocation(size_t size) {
        if (m_enabled) {
            m_memoryTracker.TrackAllocation(size);
        }
    }

    void PerformanceMonitor::TrackMemoryDeallocation(size_t size) {
        if (m_enabled) {
            m_memoryTracker.TrackDeallocation(size);
        }
    }

    PerformanceMetrics PerformanceMonitor::CollectMetricsInternal() {
        // Ensure we're initialized
        if (!m_initialized) {
            // Return empty metrics if not initialized
            PerformanceMetrics emptyMetrics = {};
            return emptyMetrics;
        }
        
        // Update platform-specific monitors
        if (m_cpuPlatform) {
            m_cpuPlatform->Update();
        }
        if (m_gpuPlatform) {
            m_gpuPlatform->Update();
        }
        if (m_systemPlatform) {
            m_systemPlatform->Update();
        }
        
        // Collect frame timing data
        m_currentMetrics.frameTime = GetFrameTimeInternal();
        m_currentMetrics.frameTimeAvg = GetAverageFrameTimeInternal();
        m_currentMetrics.fps = GetFPSInternal();
        m_currentMetrics.fpsAvg = GetAverageFPSInternal();
        m_currentMetrics.frameCount = m_frameCount;
        
        // Collect memory data
        m_currentMetrics.currentMemory = m_memoryTracker.GetCurrentMemory();
        m_currentMetrics.peakMemory = m_memoryTracker.GetPeakMemory();
        m_currentMetrics.totalMemory = m_memoryTracker.GetTotalMemory();
        m_currentMetrics.allocationCount = m_memoryTracker.GetAllocationCount();
        
        // Collect CPU data
        if (m_cpuPlatform) {
            m_currentMetrics.cpuUsage = m_cpuPlatform->GetCurrentUsage();
            m_currentMetrics.cpuUsageAvg = m_cpuPlatform->GetAverageUsage();
            m_currentMetrics.cpuCoreCount = m_cpuPlatform->GetCoreCount();
        } else {
            m_currentMetrics.cpuUsage = 0.0;
            m_currentMetrics.cpuUsageAvg = 0.0;
            m_currentMetrics.cpuCoreCount = 0;
        }
        
        // Collect GPU data
        if (m_gpuPlatform) {
            m_currentMetrics.gpuUsage = m_gpuPlatform->GetUsage();
            m_currentMetrics.gpuMemoryUsage = m_gpuPlatform->GetMemoryUsage();
            m_currentMetrics.gpuTemperature = m_gpuPlatform->GetTemperature();
        } else {
            m_currentMetrics.gpuUsage = 0.0;
            m_currentMetrics.gpuMemoryUsage = 0.0;
            m_currentMetrics.gpuTemperature = 0.0;
        }
        
        // Collect performance counter data
        m_currentMetrics.counters.clear();
        for (const auto& pair : m_counters) {
            m_currentMetrics.counters[pair.first] = pair.second->GetLastValue();
        }
        
        // Set timestamp
        m_currentMetrics.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        return m_currentMetrics;
    }

    PerformanceMetrics PerformanceMonitor::CollectMetrics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return CollectMetricsInternal();
    }

    void PerformanceMonitor::SetMetricsCollectionInterval(double intervalSeconds) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metricsCollectionInterval = intervalSeconds;
    }

    void PerformanceMonitor::SetMetricsCallback(MetricsCallback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metricsCallback = callback;
    }

    void PerformanceMonitor::UpdateMetrics() {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastMetricsUpdate).count();
        
        if (elapsed >= m_metricsCollectionInterval) {
            CollectMetricsInternal();
            m_lastMetricsUpdate = now;
            
            // Call callback if set
            if (m_metricsCallback) {
                m_metricsCallback(m_currentMetrics);
            }
        }
    }

    void PerformanceMonitor::LogFrameMetrics() {
        if (!m_loggingEnabled) {
            return;
        }

        double frameTime = GetFrameTimeInternal();
        double fps = GetFPSInternal();
        
        LT_CORE_DEBUG("Frame {}: {:.2f}ms ({:.1f} FPS)", m_frameCount, frameTime, fps);
    }

    void PerformanceMonitor::LogMetrics() {
        if (!m_loggingEnabled) {
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
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        
        // Use safe access to metrics with fallback values
        uint32_t frameCount = m_initialized ? m_currentMetrics.frameCount : 0;
        double fpsAvg = m_initialized ? m_currentMetrics.fpsAvg : 0.0;
        double frameTimeAvg = m_initialized ? m_currentMetrics.frameTimeAvg : 0.0;
        uint64_t currentMemory = m_initialized ? m_currentMetrics.currentMemory : 0;
        uint64_t peakMemory = m_initialized ? m_currentMetrics.peakMemory : 0;
        double cpuUsage = m_initialized ? m_currentMetrics.cpuUsage : 0.0;
        double cpuUsageAvg = m_initialized ? m_currentMetrics.cpuUsageAvg : 0.0;
        double gpuUsage = m_initialized ? m_currentMetrics.gpuUsage : 0.0;
        double gpuMemoryUsage = m_initialized ? m_currentMetrics.gpuMemoryUsage : 0.0;
        
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