#include "macOSPerformancePlatform.h"
#include "Core/Debug/Log.h"

#ifdef LT_PLATFORM_MACOS
    #include <sys/sysctl.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <chrono>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <mach/task.h>
    #include <mach/thread_act.h>
    #include <pthread.h>
#endif

namespace Limitless {

#ifdef LT_PLATFORM_MACOS

    // macOSCPUPlatform Implementation
    macOSCPUPlatform::macOSCPUPlatform()
        : m_host(MACH_PORT_NULL)
        , m_count(HOST_CPU_LOAD_INFO_COUNT)
        , m_currentUsage(0.0)
        , m_averageUsage(0.0)
        , m_coreCount(0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    macOSCPUPlatform::~macOSCPUPlatform() {
        Shutdown();
    }

    bool macOSCPUPlatform::Initialize() {
        // Get host port
        m_host = mach_host_self();
        if (m_host == MACH_PORT_NULL) {
            LT_CORE_ERROR("Failed to get mach host port");
            return false;
        }
        
        // Get CPU core count
        int cores;
        size_t size = sizeof(cores);
        if (sysctlbyname("hw.ncpu", &cores, &size, nullptr, 0) == 0) {
            m_coreCount = cores;
        }
        
        LT_CORE_INFO("macOS CPU Platform initialized with {} cores", m_coreCount);
        return true;
    }

    void macOSCPUPlatform::Shutdown() {
        // Nothing to clean up
    }

    void macOSCPUPlatform::Update() {
        if (m_host == MACH_PORT_NULL) {
            return;
        }
        
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
        
        if (elapsed < m_updateInterval) {
            return;
        }

        try {
            host_cpu_load_info cpuLoad;
            mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
            
            kern_return_t result = host_statistics(m_host, HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpuLoad), &count);
            if (result == KERN_SUCCESS) {
                unsigned long long total = cpuLoad.cpu_ticks[CPU_STATE_USER] + cpuLoad.cpu_ticks[CPU_STATE_SYSTEM] + 
                                         cpuLoad.cpu_ticks[CPU_STATE_IDLE] + cpuLoad.cpu_ticks[CPU_STATE_NICE];
                unsigned long long used = cpuLoad.cpu_ticks[CPU_STATE_USER] + cpuLoad.cpu_ticks[CPU_STATE_SYSTEM] + 
                                        cpuLoad.cpu_ticks[CPU_STATE_NICE];
                
                if (total > 0) {
                    m_currentUsage = 100.0 * static_cast<double>(used) / total;
                    m_averageUsage = (m_averageUsage + m_currentUsage) * 0.5;
                }
            } else {
                // If we can't get CPU stats, keep the last known values
                // This prevents crashes but maintains functionality
            }
        } catch (...) {
            // If any system call fails, keep the last known values
            // This prevents crashes but maintains functionality
        }

        m_lastUpdate = now;
    }

    void macOSCPUPlatform::Reset() {
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double macOSCPUPlatform::GetCurrentUsage() const {
        return m_currentUsage;
    }

    double macOSCPUPlatform::GetAverageUsage() const {
        return m_averageUsage;
    }

    uint32_t macOSCPUPlatform::GetCoreCount() const {
        return m_coreCount;
    }

    void macOSCPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // macOSGPUPlatform Implementation
    macOSGPUPlatform::macOSGPUPlatform()
        : m_available(false)
        , m_usage(0.0)
        , m_memoryUsage(0.0)
        , m_temperature(0.0)
        , m_updateInterval(1.0)
        , m_lastUpdate(std::chrono::high_resolution_clock::now()) {
    }

    macOSGPUPlatform::~macOSGPUPlatform() {
        Shutdown();
    }

    bool macOSGPUPlatform::Initialize() {
        // GPU monitoring on macOS requires additional libraries
        // For now, we'll mark it as unavailable
        m_available = false;
        LT_CORE_WARN("macOS GPU Platform not available - requires additional libraries");
        return false;
    }

    void macOSGPUPlatform::Shutdown() {
        m_available = false;
    }

    void macOSGPUPlatform::Update() {
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

    void macOSGPUPlatform::Reset() {
        m_usage = 0.0;
        m_memoryUsage = 0.0;
        m_temperature = 0.0;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    double macOSGPUPlatform::GetUsage() const {
        return m_usage;
    }

    double macOSGPUPlatform::GetMemoryUsage() const {
        return m_memoryUsage;
    }

    double macOSGPUPlatform::GetTemperature() const {
        return m_temperature;
    }

    bool macOSGPUPlatform::IsAvailable() const {
        return m_available;
    }

    void macOSGPUPlatform::SetUpdateInterval(double intervalSeconds) {
        m_updateInterval = intervalSeconds;
    }

    // macOSSystemPlatform Implementation
    macOSSystemPlatform::macOSSystemPlatform()
        : m_totalMemory(0)
        , m_availableMemory(0)
        , m_processMemory(0)
        , m_processId(0)
        , m_threadId(0) {
    }

    macOSSystemPlatform::~macOSSystemPlatform() {
        Shutdown();
    }

    bool macOSSystemPlatform::Initialize() {
        m_processId = getpid();
        // Use pthread_self() for thread ID which is more reliable on macOS
        m_threadId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
        
        // Try to update system info, but don't fail if it doesn't work
        try {
            Update();
        } catch (...) {
            // If Update() fails, we'll still return true but with default values
            LT_CORE_WARN("macOS System Platform Update failed, using default values");
        }
        
        return true;
    }

    void macOSSystemPlatform::Shutdown() {
        // Nothing to clean up
    }

    void macOSSystemPlatform::Update() {
        try {
            // Get system memory information
            uint64_t totalMem;
            size_t size = sizeof(totalMem);
            if (sysctlbyname("hw.memsize", &totalMem, &size, nullptr, 0) == 0) {
                m_totalMemory = totalMem;
            }

            // Get available memory (simplified - in practice you'd use vm_statistics)
            // For now, we'll use a rough estimate
            m_availableMemory = m_totalMemory * 0.8; // Assume 80% available

            // Get process memory information
            struct task_basic_info t_info;
            mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
            kern_return_t result = task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
            if (result == KERN_SUCCESS) {
                m_processMemory = t_info.resident_size;
            } else {
                // Fallback to a safe default
                m_processMemory = 0;
            }
        } catch (...) {
            // If any system call fails, use safe defaults
            m_totalMemory = 0;
            m_availableMemory = 0;
            m_processMemory = 0;
        }
    }

    uint64_t macOSSystemPlatform::GetTotalMemory() const {
        return m_totalMemory;
    }

    uint64_t macOSSystemPlatform::GetAvailableMemory() const {
        return m_availableMemory;
    }

    uint64_t macOSSystemPlatform::GetProcessMemory() const {
        return m_processMemory;
    }

    uint32_t macOSSystemPlatform::GetProcessId() const {
        return m_processId;
    }

    uint32_t macOSSystemPlatform::GetThreadId() const {
        return m_threadId;
    }

#endif // LT_PLATFORM_MACOS

} // namespace Limitless 