#include "Core/GPUMetricsProvider.h"
#include <mutex>

namespace Limitless {

    std::mutex GPUMetricsProvider::s_Mutex;
    uint64_t GPUMetricsProvider::s_UsedBytes = 0;
    uint64_t GPUMetricsProvider::s_TotalBytes = 0;

    void GPUMetricsProvider::SetVram(uint64_t usedBytes, uint64_t totalBytes)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_UsedBytes = usedBytes;
        s_TotalBytes = totalBytes;
    }

    void GPUMetricsProvider::GetVram(uint64_t& outUsedBytes, uint64_t& outTotalBytes)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        outUsedBytes = s_UsedBytes;
        outTotalBytes = s_TotalBytes;
    }

} // namespace Limitless
