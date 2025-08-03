#include "LinuxPerformancePlatform.h"
#include "Core/Debug/Log.h"

#ifdef LT_PLATFORM_LINUX
    #include <sys/sysinfo.h>
    #include <sys/resource.h>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/syscall.h>
    #include <fstream>
    #include <sstream>
    #include <chrono>
#endif

namespace Limitless {

#ifdef LT_PLATFORM_LINUX

    // LinuxCPUPlatform Implementation
    LinuxCPUPlatform::LinuxCPUPlatform()
        : m_lastTotalTime(0)
        , m_currentUsage(0.0)
        , m_averageUsage(0.0)
        , m_coreCount(0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    LinuxCPUPlatform::~LinuxCPUPlatform() {
        Shutdown();
    }

    bool LinuxCPUPlatform::Initialize() {
        // Use sysconf for core count as get_nprocs() is not available on all systems
        m_coreCount = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
        UpdateCpuTimes();
        LT_CORE_INFO("Linux CPU Platform initialized with {} cores", m_coreCount);
        return true;
    }

    void LinuxCPUPlatform::Shutdown() {
        // Nothing to clean up
    }

    void LinuxCPUPlatform::UpdateCpuTimes() {
        std::ifstream file("/proc/stat");
        if (file.is_open()) {
            std::string line;
            if (std::getline(file, line)) {
                std::istringstream iss(line);
                std::string cpu;
                iss >> cpu; // Skip "cpu"
                
                unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
                iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
                
                m_lastCpuTimes = {user, nice, system, idle, iowait, irq, softirq, steal};
                m_lastTotalTime = user + nice + system + idle + iowait + irq + softirq + steal;
            }
        }
    }

    void LinuxCPUPlatform::Update() {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
        
        if (elapsed < m_updateInterval) {
            return;
        }

        std::vector<unsigned long long> currentCpuTimes = m_lastCpuTimes;
        unsigned long long currentTotalTime = m_lastTotalTime;
        
        UpdateCpuTimes();
        
        unsigned long long totalDiff = m_lastTotalTime - currentTotalTime;
        unsigned long long idleDiff = m_lastCpuTimes[3] - currentCpuTimes[3];
        
        if (totalDiff > 0) {
            m_currentUsage = 100.0 * (1.0 - static_cast<double>(idleDiff) / totalDiff);
            m_averageUsage = (m_averageUsage + m_currentUsage) * 0.5;
        }

        m_lastUpdate = now;
    }

    void LinuxCPUPlatform::Reset() {
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double LinuxCPUPlatform::GetCurrentUsage() const {
        return m_currentUsage;
    }

    double LinuxCPUPlatform::GetAverageUsage() const {
        return m_averageUsage;
    }

    uint32_t LinuxCPUPlatform::GetCoreCount() const {
        return m_coreCount;
    }

    void LinuxCPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // LinuxGPUPlatform Implementation
    LinuxGPUPlatform::LinuxGPUPlatform()
        : m_available(false)
        , m_usage(0.0)
        , m_memoryUsage(0.0)
        , m_temperature(0.0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    LinuxGPUPlatform::~LinuxGPUPlatform() {
        Shutdown();
    }

    bool LinuxGPUPlatform::Initialize() {
        // GPU monitoring requires additional libraries like NVML
        // For now, we'll mark it as unavailable
        m_available = false;
        LT_CORE_WARN("Linux GPU Platform not available - requires NVML library");
        return false;
    }

    void LinuxGPUPlatform::Shutdown() {
        m_available = false;
    }

    void LinuxGPUPlatform::Update() {
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

    void LinuxGPUPlatform::Reset() {
        m_usage = 0.0;
        m_memoryUsage = 0.0;
        m_temperature = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double LinuxGPUPlatform::GetUsage() const {
        return m_usage;
    }

    double LinuxGPUPlatform::GetMemoryUsage() const {
        return m_memoryUsage;
    }

    double LinuxGPUPlatform::GetTemperature() const {
        return m_temperature;
    }

    bool LinuxGPUPlatform::IsAvailable() const {
        return m_available;
    }

    void LinuxGPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // LinuxSystemPlatform Implementation
    LinuxSystemPlatform::LinuxSystemPlatform()
        : m_totalMemory(0)
        , m_availableMemory(0)
        , m_processMemory(0)
        , m_processId(0)
        , m_threadId(0) {
    }

    LinuxSystemPlatform::~LinuxSystemPlatform() {
        Shutdown();
    }

    bool LinuxSystemPlatform::Initialize() {
        m_processId = getpid();
        // Use syscall for thread ID as gettid() is not available on all systems
        m_threadId = static_cast<uint32_t>(syscall(SYS_gettid));
        Update();
        return true;
    }

    void LinuxSystemPlatform::Shutdown() {
        // Nothing to clean up
    }

    void LinuxSystemPlatform::Update() {
        // Get system memory information
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            m_totalMemory = si.totalram * si.mem_unit;
            m_availableMemory = si.freeram * si.mem_unit;
        }

        // Get process memory information
        std::ifstream file("/proc/self/status");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.substr(0, 6) == "VmRSS:") {
                    std::istringstream iss(line.substr(6));
                    iss >> m_processMemory;
                    m_processMemory *= 1024; // Convert KB to bytes
                    break;
                }
            }
        }
    }

    uint64_t LinuxSystemPlatform::GetTotalMemory() const {
        return m_totalMemory;
    }

    uint64_t LinuxSystemPlatform::GetAvailableMemory() const {
        return m_availableMemory;
    }

    uint64_t LinuxSystemPlatform::GetProcessMemory() const {
        return m_processMemory;
    }

    uint32_t LinuxSystemPlatform::GetProcessId() const {
        return m_processId;
    }

    uint32_t LinuxSystemPlatform::GetThreadId() const {
        return m_threadId;
    }

#endif // LT_PLATFORM_LINUX

} // namespace Limitless 