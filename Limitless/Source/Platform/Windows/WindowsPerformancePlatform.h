#pragma once

#include "Platform/PerformancePlatform.h"
#include <chrono>

#ifdef LT_PLATFORM_WINDOWS
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#endif

namespace Limitless {

#ifdef LT_PLATFORM_WINDOWS

    /**
     * @brief Windows-specific CPU monitoring implementation
     */
    class WindowsCPUPlatform : public ICPUPlatform {
    public:
        WindowsCPUPlatform();
        virtual ~WindowsCPUPlatform();
        
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;
        void Reset() override;
        
        double GetCurrentUsage() const override;
        double GetAverageUsage() const override;
        uint32_t GetCoreCount() const override;
        void SetUpdateInterval(double intervalSeconds) override;

    private:
        PDH_HQUERY m_query;
        PDH_HCOUNTER m_counter;
        bool m_initialized;
        double m_currentUsage;
        double m_averageUsage;
        uint32_t m_coreCount;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
    };

    /**
     * @brief Windows-specific GPU monitoring implementation
     */
    class WindowsGPUPlatform : public IGPUPlatform {
    public:
        WindowsGPUPlatform();
        virtual ~WindowsGPUPlatform();
        
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;
        void Reset() override;
        
        double GetUsage() const override;
        double GetMemoryUsage() const override;
        double GetTemperature() const override;
        bool IsAvailable() const override;
        void SetUpdateInterval(double intervalSeconds) override;

    private:
        bool m_available;
        double m_usage;
        double m_memoryUsage;
        double m_temperature;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
    };

    /**
     * @brief Windows-specific system monitoring implementation
     */
    class WindowsSystemPlatform : public ISystemPlatform {
    public:
        WindowsSystemPlatform();
        virtual ~WindowsSystemPlatform();
        
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;
        
        uint64_t GetTotalMemory() const override;
        uint64_t GetAvailableMemory() const override;
        uint64_t GetProcessMemory() const override;
        uint32_t GetProcessId() const override;
        uint32_t GetThreadId() const override;

    private:
        uint64_t m_totalMemory;
        uint64_t m_availableMemory;
        uint64_t m_processMemory;
        uint32_t m_processId;
        uint32_t m_threadId;
    };

#endif // LT_PLATFORM_WINDOWS

} // namespace Limitless 