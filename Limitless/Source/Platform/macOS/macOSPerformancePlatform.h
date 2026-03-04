#pragma once

#include "Platform/PerformancePlatform.h"
#include <chrono>
#ifdef LT_PLATFORM_MACOS
    #include <mach/mach.h>
    #include <mach/mach_host.h>
#endif

namespace Limitless {

#ifdef LT_PLATFORM_MACOS

    /// macOS-specific CPU monitoring implementation.
    class macOSCPUPlatform : public ICPUPlatform {
    public:
        macOSCPUPlatform();
        virtual ~macOSCPUPlatform();
        
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;
        void Reset() override;
        
        double GetCurrentUsage() const override;
        double GetAverageUsage() const override;
        uint32_t GetCoreCount() const override;
        void SetUpdateInterval(double intervalSeconds) override;

    private:
        host_t m_Host;
        mach_msg_type_number_t m_Count;
        double m_CurrentUsage;
        double m_AverageUsage;
        uint32_t m_CoreCount;
        double m_UpdateInterval;
        std::chrono::high_resolution_clock::time_point m_LastUpdate;
    };

    /// macOS-specific GPU monitoring implementation.
    class macOSGPUPlatform : public IGPUPlatform {
    public:
        macOSGPUPlatform();
        virtual ~macOSGPUPlatform();
        
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

    /// macOS-specific system monitoring implementation.
    class macOSSystemPlatform : public ISystemPlatform {
    public:
        macOSSystemPlatform();
        virtual ~macOSSystemPlatform();
        
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

#endif // LT_PLATFORM_MACOS

} // namespace Limitless 