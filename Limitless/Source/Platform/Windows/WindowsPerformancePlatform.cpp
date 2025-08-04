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
        : m_initialized(false)
        , m_currentUsage(0.0)
        , m_averageUsage(0.0)
        , m_coreCount(0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    WindowsCPUPlatform::~WindowsCPUPlatform() {
        Shutdown();
    }

    bool WindowsCPUPlatform::Initialize() {
        LT_VERIFY(!m_initialized, "Windows CPU Platform already initialized");
        
        if (m_initialized) {
            return true;
        }

        // Get CPU core count
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        m_coreCount = sysInfo.dwNumberOfProcessors;
        
        LT_VERIFY(m_coreCount > 0, "Invalid CPU core count");

        // Initialize PDH for CPU monitoring
        if (PdhOpenQuery(nullptr, 0, &m_query) == ERROR_SUCCESS) {
            if (PdhAddCounter(m_query, L"\\Processor(_Total)\\% Processor Time", 0, &m_counter) == ERROR_SUCCESS) {
                PdhCollectQueryData(m_query);
                m_initialized = true;
                LT_CORE_INFO("Windows CPU Platform initialized with {} cores", m_coreCount);
                return true;
            }
        }

        std::string errorMsg = "Failed to initialize Windows CPU Platform - PDH initialization failed";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("WindowsCPUPlatform::Initialize");
        error.SetClassName("WindowsCPUPlatform");
        error.SetModuleName("Platform/Windows");
        error.AddContext("core_count", std::to_string(m_coreCount));
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
    }

    void WindowsCPUPlatform::Shutdown() {
        if (m_initialized) {
            PdhCloseQuery(m_query);
            m_initialized = false;
        }
    }

    void WindowsCPUPlatform::Update() {
        LT_VERIFY(m_initialized, "Windows CPU Platform not initialized");
        
        if (!m_initialized) {
            return;
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
        
        if (elapsed < m_updateInterval) {
            return;
        }

        PDH_FMT_COUNTERVALUE value;
        if (PdhCollectQueryData(m_query) == ERROR_SUCCESS) {
            if (PdhGetFormattedCounterValue(m_counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
                m_currentUsage = value.doubleValue;
                m_averageUsage = (m_averageUsage + m_currentUsage) * 0.5; // Simple moving average
            }
            else
            {
                std::string errorMsg = "Failed to get formatted counter value for CPU usage";
                PlatformError error(errorMsg, std::source_location::current());
                error.SetFunctionName("WindowsCPUPlatform::Update");
                error.SetClassName("WindowsCPUPlatform");
                error.SetModuleName("Platform/Windows");
                error.AddContext("current_usage", std::to_string(m_currentUsage));
                error.AddContext("average_usage", std::to_string(m_averageUsage));
                
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

        m_lastUpdate = now;
    }

    void WindowsCPUPlatform::Reset() {
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double WindowsCPUPlatform::GetCurrentUsage() const {
        return m_currentUsage;
    }

    double WindowsCPUPlatform::GetAverageUsage() const {
        return m_averageUsage;
    }

    uint32_t WindowsCPUPlatform::GetCoreCount() const {
        return m_coreCount;
    }

    void WindowsCPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // WindowsGPUPlatform Implementation
    WindowsGPUPlatform::WindowsGPUPlatform()
        : m_available(false)
        , m_usage(0.0)
        , m_memoryUsage(0.0)
        , m_temperature(0.0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    WindowsGPUPlatform::~WindowsGPUPlatform() {
        Shutdown();
    }

    bool WindowsGPUPlatform::Initialize() {
        // GPU monitoring requires additional libraries like NVML or AMD ADL
        // For now, we'll mark it as unavailable
        m_available = false;
        LT_CORE_WARN("Windows GPU Platform not available - requires NVML or AMD ADL libraries");
        return false;
    }

    void WindowsGPUPlatform::Shutdown() {
        m_available = false;
    }

    void WindowsGPUPlatform::Update() {
        if (!m_available) {
            return;
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
        
        if (elapsed < m_updateInterval) {
            return;
        }

        // GPU monitoring implementation would go here
        // This requires platform-specific GPU monitoring libraries
        
        m_lastUpdate = now;
    }

    void WindowsGPUPlatform::Reset() {
        m_usage = 0.0;
        m_memoryUsage = 0.0;
        m_temperature = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double WindowsGPUPlatform::GetUsage() const {
        return m_usage;
    }

    double WindowsGPUPlatform::GetMemoryUsage() const {
        return m_memoryUsage;
    }

    double WindowsGPUPlatform::GetTemperature() const {
        return m_temperature;
    }

    bool WindowsGPUPlatform::IsAvailable() const {
        return m_available;
    }

    void WindowsGPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // WindowsSystemPlatform Implementation
    WindowsSystemPlatform::WindowsSystemPlatform()
        : m_totalMemory(0)
        , m_availableMemory(0)
        , m_processMemory(0)
        , m_processId(0)
        , m_threadId(0) {
    }

    WindowsSystemPlatform::~WindowsSystemPlatform() {
        Shutdown();
    }

    bool WindowsSystemPlatform::Initialize() {
        m_processId = GetCurrentProcessId();
        m_threadId = GetCurrentThreadId();
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
            m_totalMemory = memInfo.ullTotalPhys;
            m_availableMemory = memInfo.ullAvailPhys;
        }

        // Get process memory information
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            m_processMemory = pmc.WorkingSetSize;
        }
    }

    uint64_t WindowsSystemPlatform::GetTotalMemory() const {
        return m_totalMemory;
    }

    uint64_t WindowsSystemPlatform::GetAvailableMemory() const {
        return m_availableMemory;
    }

    uint64_t WindowsSystemPlatform::GetProcessMemory() const {
        return m_processMemory;
    }

    uint32_t WindowsSystemPlatform::GetProcessId() const {
        return m_processId;
    }

    uint32_t WindowsSystemPlatform::GetThreadId() const {
        return m_threadId;
    }

#endif // LT_PLATFORM_WINDOWS

} // namespace Limitless 