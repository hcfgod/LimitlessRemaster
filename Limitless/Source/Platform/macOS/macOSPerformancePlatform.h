#pragma once

#include "Platform/PerformancePlatform.h"
#include <chrono>
#ifdef LT_PLATFORM_MACOS
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <pthread.h>
#endif

namespace Limitless {

#ifdef LT_PLATFORM_MACOS

    /**
     * @brief macOS-specific CPU monitoring implementation
     */
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
        host_t m_host;
        mach_msg_type_number_t m_count;
        double m_currentUsage;
        double m_averageUsage;
        uint32_t m_coreCount;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
    };

    /**
     * @brief macOS-specific GPU monitoring implementation
     */
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
        bool m_available;
        double m_usage;
        double m_memoryUsage;
        double m_temperature;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
    };

    /**
     * @brief macOS-specific system monitoring implementation
     */
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
        uint64_t m_totalMemory;
        uint64_t m_availableMemory;
        uint64_t m_processMemory;
        uint32_t m_processId;
        uint32_t m_threadId;
    };

#endif // LT_PLATFORM_MACOS

} // namespace Limitless 