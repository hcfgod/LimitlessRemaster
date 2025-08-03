#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Limitless {

    // Forward declarations
    struct CPUMetrics;
    struct GPUMetrics;
    struct SystemMetrics;

    /**
     * @brief Platform-specific CPU monitoring interface
     */
    class ICPUPlatform {
    public:
        virtual ~ICPUPlatform() = default;
        
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;
        virtual void Reset() = 0;
        
        virtual double GetCurrentUsage() const = 0;
        virtual double GetAverageUsage() const = 0;
        virtual uint32_t GetCoreCount() const = 0;
        virtual void SetUpdateInterval(double intervalSeconds) = 0;
    };

    /**
     * @brief Platform-specific GPU monitoring interface
     */
    class IGPUPlatform {
    public:
        virtual ~IGPUPlatform() = default;
        
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;
        virtual void Reset() = 0;
        
        virtual double GetUsage() const = 0;
        virtual double GetMemoryUsage() const = 0;
        virtual double GetTemperature() const = 0;
        virtual bool IsAvailable() const = 0;
        virtual void SetUpdateInterval(double intervalSeconds) = 0;
    };

    /**
     * @brief Platform-specific system monitoring interface
     */
    class ISystemPlatform {
    public:
        virtual ~ISystemPlatform() = default;
        
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void Update() = 0;
        
        virtual uint64_t GetTotalMemory() const = 0;
        virtual uint64_t GetAvailableMemory() const = 0;
        virtual uint64_t GetProcessMemory() const = 0;
        virtual uint32_t GetProcessId() const = 0;
        virtual uint32_t GetThreadId() const = 0;
    };

    /**
     * @brief Factory for creating platform-specific performance monitoring objects
     */
    class PerformancePlatformFactory {
    public:
        static std::unique_ptr<ICPUPlatform> CreateCPUPlatform();
        static std::unique_ptr<IGPUPlatform> CreateGPUPlatform();
        static std::unique_ptr<ISystemPlatform> CreateSystemPlatform();
    };

} // namespace Limitless 