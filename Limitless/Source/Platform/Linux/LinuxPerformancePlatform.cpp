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
        : m_LastTotalTime(0)
        , m_CurrentUsage(0.0)
        , m_AverageUsage(0.0)
        , m_CoreCount(0)
        , m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    LinuxCPUPlatform::~LinuxCPUPlatform() {
        Shutdown();
    }

    bool LinuxCPUPlatform::Initialize() {
        // Use sysconf for core count as get_nprocs() is not available on all systems
        m_CoreCount = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
        UpdateCpuTimes();
        LT_CORE_INFO("Linux CPU Platform initialized with {} cores", m_CoreCount);
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
                
                m_LastCpuTimes = {user, nice, system, idle, iowait, irq, softirq, steal};
                m_LastTotalTime = user + nice + system + idle + iowait + irq + softirq + steal;
            }
        }
    }

    void LinuxCPUPlatform::Update() {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_LastUpdate).count();
        
        if (elapsed < m_UpdateInterval) {
            return;
        }

        std::vector<unsigned long long> currentCpuTimes = m_LastCpuTimes;
        unsigned long long currentTotalTime = m_LastTotalTime;
        
        UpdateCpuTimes();
        
        unsigned long long totalDiff = m_LastTotalTime - currentTotalTime;
        unsigned long long idleDiff = m_LastCpuTimes[3] - currentCpuTimes[3];
        
        if (totalDiff > 0) {
            m_CurrentUsage = 100.0 * (1.0 - static_cast<double>(idleDiff) / totalDiff);
            m_AverageUsage = (m_AverageUsage + m_CurrentUsage) * 0.5;
        }

        m_LastUpdate = now;
    }

    void LinuxCPUPlatform::Reset() {
        m_CurrentUsage = 0.0;
        m_AverageUsage = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double LinuxCPUPlatform::GetCurrentUsage() const {
        return m_CurrentUsage;
    }

    double LinuxCPUPlatform::GetAverageUsage() const {
        return m_AverageUsage;
    }

    uint32_t LinuxCPUPlatform::GetCoreCount() const {
        return m_CoreCount;
    }

    void LinuxCPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
    }

    // LinuxGPUPlatform Implementation
    LinuxGPUPlatform::LinuxGPUPlatform()
        : m_Available(false)
        , m_Usage(0.0)
        , m_MemoryUsage(0.0)
        , m_Temperature(0.0)
        , m_UpdateInterval(1.0)
        , m_LastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    LinuxGPUPlatform::~LinuxGPUPlatform() {
        Shutdown();
    }

    bool LinuxGPUPlatform::Initialize() {
        // GPU monitoring requires additional libraries like NVML
        // For now, we'll mark it as unavailable
        m_Available = false;
        LT_CORE_WARN("Linux GPU Platform not available - requires NVML library");
        return false;
    }

    void LinuxGPUPlatform::Shutdown() {
        m_Available = false;
    }

    void LinuxGPUPlatform::Update() {
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

    void LinuxGPUPlatform::Reset() {
        m_Usage = 0.0;
        m_MemoryUsage = 0.0;
        m_Temperature = 0.0;
        m_LastUpdate = std::chrono::high_resolution_clock::now();
    }

    double LinuxGPUPlatform::GetUsage() const {
        return m_Usage;
    }

    double LinuxGPUPlatform::GetMemoryUsage() const {
        return m_MemoryUsage;
    }

    double LinuxGPUPlatform::GetTemperature() const {
        return m_Temperature;
    }

    bool LinuxGPUPlatform::IsAvailable() const {
        return m_Available;
    }

    void LinuxGPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_UpdateInterval = intervalSeconds;
    }

    // LinuxSystemPlatform Implementation
    LinuxSystemPlatform::LinuxSystemPlatform()
        : m_TotalMemory(0)
        , m_AvailableMemory(0)
        , m_ProcessMemory(0)
        , m_ProcessId(0)
        , m_ThreadId(0) {
    }

    LinuxSystemPlatform::~LinuxSystemPlatform() {
        Shutdown();
    }

    bool LinuxSystemPlatform::Initialize() {
        m_ProcessId = getpid();
        // Use syscall for thread ID as gettid() is not available on all systems
        m_ThreadId = static_cast<uint32_t>(syscall(SYS_gettid));
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
            m_TotalMemory = si.totalram * si.mem_unit;
            m_AvailableMemory = si.freeram * si.mem_unit;
        }

        // Get process memory information
        std::ifstream file("/proc/self/status");
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.substr(0, 6) == "VmRSS:") {
                    std::istringstream iss(line.substr(6));
                    iss >> m_ProcessMemory;
                    m_ProcessMemory *= 1024; // Convert KB to bytes
                    break;
                }
            }
        }
    }

    uint64_t LinuxSystemPlatform::GetTotalMemory() const {
        return m_TotalMemory;
    }

    uint64_t LinuxSystemPlatform::GetAvailableMemory() const {
        return m_AvailableMemory;
    }

    uint64_t LinuxSystemPlatform::GetProcessMemory() const {
        return m_ProcessMemory;
    }

    uint32_t LinuxSystemPlatform::GetProcessId() const {
        return m_ProcessId;
    }

    uint32_t LinuxSystemPlatform::GetThreadId() const {
        return m_ThreadId;
    }

#endif // LT_PLATFORM_LINUX

} // namespace Limitless 