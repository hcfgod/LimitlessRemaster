#pragma once

#include <cstdint>
#include <mutex>

namespace Limitless {

    /// Thread-safe provider for GPU VRAM metrics (used/total bytes).
    /// Updated by the OpenGL layer when a context is current; read by platform
    /// GPU monitors and PerformanceMonitor. No vendor libraries required.
    class GPUMetricsProvider {
    public:
        /// Set VRAM used and total (bytes). Call from render thread with GL context current.
        static void SetVram(uint64_t usedBytes, uint64_t totalBytes);

        /// Get last set VRAM values. Thread-safe.
        static void GetVram(uint64_t& outUsedBytes, uint64_t& outTotalBytes);

    private:
        static std::mutex s_Mutex;
        static uint64_t s_UsedBytes;
        static uint64_t s_TotalBytes;
    };

} // namespace Limitless
