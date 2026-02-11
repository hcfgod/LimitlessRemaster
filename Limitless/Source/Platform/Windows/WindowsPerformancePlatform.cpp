#include "WindowsPerformancePlatform.h"
#include "Core/Debug/Log.h"
#include "Core/Error.h"
#include <chrono>
#include <sstream>

#ifdef LT_PLATFORM_WINDOWS
    #pragma comment(lib, "pdh.lib")
#endif

namespace Limitless {

#ifdef LT_PLATFORM_WINDOWS

    // WindowsCPUPlatform Implementation
    WindowsCPUPlatform::WindowsCPUPlatform()
        : m_Initialized(false)
        , m_CurrentUsage(0.0)
        , m_AverageUsage(0.0)
        , m_CoreCount(0)
        , m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    WindowsCPUPlatform::~WindowsCPUPlatform() {
        Shutdown();
    }

    bool WindowsCPUPlatform::Initialize() {
        LT_VERIFY(!m_Initialized, "Windows CPU Platform already initialized");
        
        if (m_Initialized) {
            return true;
        }

        // Get CPU core count
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        m_CoreCount = sysInfo.dwNumberOfProcessors;
        
        LT_VERIFY(m_CoreCount > 0, "Invalid CPU core count");

        // Initialize PDH for CPU monitoring
        if (PdhOpenQuery(nullptr, 0, &m_Query) == ERROR_SUCCESS) {
            if (PdhAddCounter(m_Query, L"\\Processor(_Total)\\% Processor Time", 0, &m_Counter) == ERROR_SUCCESS) {
                PdhCollectQueryData(m_Query);
                m_Initialized = true;
                LT_CORE_INFO("Windows CPU Platform initialized with {} cores", m_CoreCount);
                return true;
            }
        }

        std::string errorMsg = "Failed to initialize Windows CPU Platform - PDH initialization failed";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("WindowsCPUPlatform::Initialize");
        error.SetClassName("WindowsCPUPlatform");
        error.SetModuleName("Platform/Windows");
        error.AddContext("core_count", std::to_string(m_CoreCount));
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
    }

    void WindowsCPUPlatform::Shutdown() {
        if (m_Initialized) {
            PdhCloseQuery(m_Query);
            m_Initialized = false;
        }
    }

    void WindowsCPUPlatform::Update() {
        LT_VERIFY(m_Initialized, "Windows CPU Platform not initialized");
        
        if (!m_Initialized) {
            return;
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_LastUpdate).count();
        
        if (elapsed < m_UpdateInterval) {
            return;
        }

        PDH_FMT_COUNTERVALUE value;
        if (PdhCollectQueryData(m_Query) == ERROR_SUCCESS) {
            if (PdhGetFormattedCounterValue(m_Counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
                m_CurrentUsage = value.doubleValue;
                m_AverageUsage = (m_AverageUsage + m_CurrentUsage) * 0.5; // Simple moving average
            }
            else
            {
                std::string errorMsg = "Failed to get formatted counter value for CPU usage";
                PlatformError error(errorMsg, std::source_location::current());
                error.SetFunctionName("WindowsCPUPlatform::Update");
                error.SetClassName("WindowsCPUPlatform");
                error.SetModuleName("Platform/Windows");
                error.AddContext("current_usage", std::to_string(m_CurrentUsage));
                error.AddContext("average_usage", std::to_string(m_AverageUsage));
                
                LT_CORE_ERROR("{}", errorMsg);
                Error::LogError(error);
                LT_THROW_PLATFORM_ERROR(errorMsg);
            }
        }
        else
        {
            std::string errorMsg = "Failed to collect query data for CPU usage";
            PlatformError error(errorMsg, std::source_location::current());
            error.SetFunctionName("WindowsCPUPlatform::Update");
            error.SetClassName("WindowsCPUPlatform");
            error.SetModuleName("Platform/Windows");
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_PLATFORM_ERROR(errorMsg);
        }

        m_LastUpdate = now;
    }

    void WindowsCPUPlatform::Reset() {
        m_CurrentUsage = 0.0;
        m_AverageUsage = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double WindowsCPUPlatform::GetCurrentUsage() const {
        return m_CurrentUsage;
    }

    double WindowsCPUPlatform::GetAverageUsage() const {
        return m_AverageUsage;
    }

    uint32_t WindowsCPUPlatform::GetCoreCount() const {
        return m_CoreCount;
    }

    void WindowsCPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
    }

    // WindowsGPUPlatform Implementation
    WindowsGPUPlatform::WindowsGPUPlatform()
        : m_Available(false)
        , m_Usage(0.0)
        , m_MemoryUsage(0.0)
        , m_Temperature(0.0)
        , m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    WindowsGPUPlatform::~WindowsGPUPlatform() {
        Shutdown();
    }

    bool WindowsGPUPlatform::Initialize() {
        // GPU monitoring requires additional libraries like NVML or AMD ADL
        // For now, we'll mark it as unavailable
        m_Available = false;
        LT_CORE_WARN("Windows GPU Platform not available - requires NVML or AMD ADL libraries");
        return false;
    }

    void WindowsGPUPlatform::Shutdown() {
        m_Available = false;
    }

    void WindowsGPUPlatform::Update() {
        if (!m_Available) {
            return;
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_LastUpdate).count();
        
        if (elapsed < m_UpdateInterval) {
            return;
        }

        // GPU monitoring implementation would go here
        // This requires platform-specific GPU monitoring libraries
        
        m_LastUpdate = now;
    }

    void WindowsGPUPlatform::Reset() {
        m_Usage = 0.0;
        m_MemoryUsage = 0.0;
        m_Temperature = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double WindowsGPUPlatform::GetUsage() const {
        return m_Usage;
    }

    double WindowsGPUPlatform::GetMemoryUsage() const {
        return m_MemoryUsage;
    }

    double WindowsGPUPlatform::GetTemperature() const {
        return m_Temperature;
    }

    bool WindowsGPUPlatform::IsAvailable() const {
        return m_Available;
    }

    void WindowsGPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
    }

    // WindowsSystemPlatform Implementation
    WindowsSystemPlatform::WindowsSystemPlatform()
        : m_TotalMemory(0)
        , m_AvailableMemory(0)
        , m_ProcessMemory(0)
        , m_ProcessId(0)
        , m_ThreadId(0) {
    }

    WindowsSystemPlatform::~WindowsSystemPlatform() {
        Shutdown();
    }

    bool WindowsSystemPlatform::Initialize() {
        m_ProcessId = GetCurrentProcessId();
        m_ThreadId = GetCurrentThreadId();
        Update();
        return true;
    }

    void WindowsSystemPlatform::Shutdown() {
        // Nothing to clean up
    }

    void WindowsSystemPlatform::Update() {
        // Get system memory information
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            m_TotalMemory = memInfo.ullTotalPhys;
            m_AvailableMemory = memInfo.ullAvailPhys;
        }

        // Get process memory information
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            m_ProcessMemory = pmc.WorkingSetSize;
        }
    }

    uint64_t WindowsSystemPlatform::GetTotalMemory() const {
        return m_TotalMemory;
    }

    uint64_t WindowsSystemPlatform::GetAvailableMemory() const {
        return m_AvailableMemory;
    }

    uint64_t WindowsSystemPlatform::GetProcessMemory() const {
        return m_ProcessMemory;
    }

    uint32_t WindowsSystemPlatform::GetProcessId() const {
        return m_ProcessId;
    }

    uint32_t WindowsSystemPlatform::GetThreadId() const {
        return m_ThreadId;
    }

#endif // LT_PLATFORM_WINDOWS

} // namespace Limitless 