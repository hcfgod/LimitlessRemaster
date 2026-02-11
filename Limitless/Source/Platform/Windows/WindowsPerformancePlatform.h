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
        PDH_HQUERY m_Query;
        PDH_HCOUNTER m_Counter;
        bool m_Initialized;
        double m_CurrentUsage;
        double m_AverageUsage;
        uint32_t m_CoreCount;
        double m_UpdateInterval;
        std::chrono::high_resolution_clock::time_point m_LastUpdate;
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
        bool m_Available;
        double m_Usage;
        double m_MemoryUsage;
        double m_Temperature;
        double m_UpdateInterval;
        std::chrono::high_resolution_clock::time_point m_LastUpdate;
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
        uint64_t m_TotalMemory;
        uint64_t m_AvailableMemory;
        uint64_t m_ProcessMemory;
        uint32_t m_ProcessId;
        uint32_t m_ThreadId;
    };

#endif // LT_PLATFORM_WINDOWS

} // namespace Limitless 