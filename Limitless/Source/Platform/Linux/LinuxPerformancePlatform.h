#pragma once

#include "Platform/PerformancePlatform.h"
#include <vector>
#include <chrono>
#include <fstream>

namespace Limitless {

#ifdef LT_PLATFORM_LINUX

    /**
     * @brief Linux-specific CPU monitoring implementation
     */
    class LinuxCPUPlatform : public ICPUPlatform {
    public:
        LinuxCPUPlatform();
        virtual ~LinuxCPUPlatform();
        
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;
        void Reset() override;
        
        double GetCurrentUsage() const override;
        double GetAverageUsage() const override;
        uint32_t GetCoreCount() const override;
        void SetUpdateInterval(double intervalSeconds) override;

    private:
        void UpdateCpuTimes();
        
        std::vector<unsigned long long> m_lastCpuTimes;
        unsigned long long m_lastTotalTime;
        double m_currentUsage;
        double m_averageUsage;
        uint32_t m_coreCount;
        double m_updateInterval;
        std::chrono::high_resolution_clock::time_point m_lastUpdate;
    };

    /**
     * @brief Linux-specific GPU monitoring implementation
     */
    class LinuxGPUPlatform : public IGPUPlatform {
    public:
        LinuxGPUPlatform();
        virtual ~LinuxGPUPlatform();
        
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
     * @brief Linux-specific system monitoring implementation
     */
    class LinuxSystemPlatform : public ISystemPlatform {
    public:
        LinuxSystemPlatform();
        virtual ~LinuxSystemPlatform();
        
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

#endif // LT_PLATFORM_LINUX

} // namespace Limitless 