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
        
        // Initialize with safe defaults
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_coreCount = 1; // Safe default
    }

    macOSCPUPlatform::~macOSCPUPlatform() {
        Shutdown();
    }

    bool macOSCPUPlatform::Initialize() {
        // Reset to safe defaults first
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_coreCount = 1;
        
        // Get host port with error checking and ARM64 safety
        try {
            m_host = mach_host_self();
        } catch (...) {
            m_host = MACH_PORT_NULL;
        }
        
        if (m_host == MACH_PORT_NULL || m_host == MACH_PORT_DEAD) {
            LT_CORE_ERROR("Failed to get mach host port");
            return false;
        }
        
        // Get CPU core count with safer sysctl call and ARM64 handling
        int cores = 0;
        size_t size = sizeof(cores);
        
        // Try different sysctl calls for ARM64 compatibility
        if (sysctlbyname("hw.ncpu", &cores, &size, nullptr, 0) == 0 && cores > 0) {
            m_coreCount = static_cast<uint32_t>(cores);
        } else if (sysctlbyname("hw.logicalcpu", &cores, &size, nullptr, 0) == 0 && cores > 0) {
            m_coreCount = static_cast<uint32_t>(cores);
        } else if (sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0) == 0 && cores > 0) {
            m_coreCount = static_cast<uint32_t>(cores);
        } else {
            // Final fallback for ARM64
            m_coreCount = 1;
        }
        
        // Additional safety checks for ARM64
        if (m_coreCount == 0 || m_coreCount > 128) { // Reasonable upper limit
            m_coreCount = 1;
        }
        
        // Validate host port is still valid
        if (m_host == MACH_PORT_NULL || m_host == MACH_PORT_DEAD) {
            LT_CORE_ERROR("Host port became invalid during initialization");
            return false;
        }
        
        LT_CORE_INFO("macOS CPU Platform initialized with {} cores", m_coreCount);
        return true;
    }

    void macOSCPUPlatform::Shutdown() {
        // Reset all values to safe defaults
        m_currentUsage = 0.0;
        m_averageUsage = 0.0;
        m_coreCount = 0;
        m_host = MACH_PORT_NULL;
        m_lastUpdate = std::chrono::high_resolution_clock::now();
    }

    void macOSCPUPlatform::Update() {
        // Additional safety check for ARM64
        if (m_host == MACH_PORT_NULL || m_host == MACH_PORT_DEAD) {
            return;
        }
        
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastUpdate).count();
        
        if (elapsed < m_updateInterval) {
            return;
        }

        // Use safer CPU statistics collection with extensive error handling
        host_cpu_load_info cpuLoad = {0}; // Initialize to zero
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        
        // Additional validation for ARM64
        if (m_host == MACH_PORT_NULL || m_host == MACH_PORT_DEAD) {
            return;
        }
        
        // Try to get CPU statistics with error handling
        kern_return_t result = KERN_FAILURE;
        try {
            result = host_statistics(m_host, HOST_CPU_LOAD_INFO, 
                                   reinterpret_cast<host_info_t>(&cpuLoad), &count);
        } catch (...) {
            // If host_statistics throws an exception, return safely
            return;
        }
        
        if (result == KERN_SUCCESS && count == HOST_CPU_LOAD_INFO_COUNT) {
            // Validate CPU tick values before calculation
            bool validTicks = true;
            for (int i = 0; i < CPU_STATE_MAX; ++i) {
                if (cpuLoad.cpu_ticks[i] < 0) {
                    validTicks = false;
                    break;
                }
            }
            
            if (validTicks) {
                // Calculate CPU usage with overflow protection
                uint64_t user = cpuLoad.cpu_ticks[CPU_STATE_USER];
                uint64_t system = cpuLoad.cpu_ticks[CPU_STATE_SYSTEM];
                uint64_t idle = cpuLoad.cpu_ticks[CPU_STATE_IDLE];
                uint64_t nice = cpuLoad.cpu_ticks[CPU_STATE_NICE];
                
                // Check for overflow
                if (user <= UINT64_MAX - system && 
                    (user + system) <= UINT64_MAX - nice &&
                    (user + system + nice) <= UINT64_MAX - idle) {
                    
                    uint64_t total = user + system + idle + nice;
                    uint64_t used = user + system + nice;
                    
                    if (total > 0) {
                        m_currentUsage = 100.0 * static_cast<double>(used) / static_cast<double>(total);
                        m_averageUsage = (m_averageUsage + m_currentUsage) * 0.5;
                    }
                }
            }
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
        
        // Initialize with safe defaults
        m_totalMemory = 0;
        m_availableMemory = 0;
        m_processMemory = 0;
        
        // Try to update system info, but don't fail if it doesn't work
        Update();
        
        return true;
    }

    void macOSSystemPlatform::Shutdown() {
        // Nothing to clean up
    }

    void macOSSystemPlatform::Update() {
        // Get system memory information with safer sysctl calls
        uint64_t totalMem = 0;
        size_t size = sizeof(totalMem);
        if (sysctlbyname("hw.memsize", &totalMem, &size, nullptr, 0) == 0 && totalMem > 0) {
            m_totalMemory = totalMem;
        }

        // Get available memory using vm_statistics (more reliable than estimation)
        vm_size_t pageSize = 0;
        host_t host = mach_host_self();
        if (host != MACH_PORT_NULL && host != MACH_PORT_DEAD) {
            if (host_page_size(host, &pageSize) == KERN_SUCCESS && pageSize > 0) {
                vm_statistics64_data_t vmStats = {0}; // Initialize to zero
                mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
                if (host_statistics64(host, HOST_VM_INFO64, 
                                    reinterpret_cast<host_info64_t>(&vmStats), &count) == KERN_SUCCESS) {
                    uint64_t freePages = vmStats.free_count + vmStats.inactive_count;
                    m_availableMemory = static_cast<uint64_t>(freePages) * pageSize;
                } else {
                    // Fallback to rough estimation
                    m_availableMemory = m_totalMemory * 0.8;
                }
            } else {
                // Fallback to rough estimation
                m_availableMemory = m_totalMemory * 0.8;
            }
        } else {
            // Fallback to rough estimation
            m_availableMemory = m_totalMemory * 0.8;
        }

        // Get process memory information with safer task_info call
        struct task_basic_info t_info = {0}; // Initialize to zero
        mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
        task_t task = mach_task_self();
        if (task != MACH_PORT_NULL && task != MACH_PORT_DEAD) {
            kern_return_t result = task_info(task, TASK_BASIC_INFO, 
                                           reinterpret_cast<task_info_t>(&t_info), &t_info_count);
            if (result == KERN_SUCCESS && t_info_count == TASK_BASIC_INFO_COUNT) {
                m_processMemory = t_info.resident_size;
            } else {
                // Fallback to a safe default
                m_processMemory = 0;
            }
        } else {
            // Fallback to a safe default
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