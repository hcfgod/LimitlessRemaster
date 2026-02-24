#include "Graphics/OpenGL/OpenGLGPUMetrics.h"
#include "Core/GPUMetricsProvider.h"

#if __has_include(<glad/glad.h>)
    #include <glad/glad.h>
#endif

// Extension constants not always in GLAD headers
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
    #define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
    #define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#endif
#ifndef GL_VBO_FREE_MEMORY_ATI
    #define GL_VBO_FREE_MEMORY_ATI 0x87FB
#endif

namespace Limitless {

    void UpdateGPUMetricsFromOpenGL()
    {
#if __has_include(<glad/glad.h>)
        uint64_t usedBytes = 0;
        uint64_t totalBytes = 0;

        // NVIDIA: GL_NVX_gpu_memory_info (values in KB)
        GLint nvTotalKb = 0;
        GLint nvAvailKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &nvTotalKb);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &nvAvailKb);
        if (nvTotalKb > 0 && nvAvailKb >= 0)
        {
            const uint64_t totalKb = static_cast<uint64_t>(nvTotalKb);
            const uint64_t availKb = static_cast<uint64_t>(nvAvailKb);
            totalBytes = totalKb * 1024;
            usedBytes = (totalKb > availKb) ? ((totalKb - availKb) * 1024) : 0;
            GPUMetricsProvider::SetVram(usedBytes, totalBytes);
            return;
        }

        // AMD: GL_ATI_meminfo (four ints: total free, largest free block, num blocks, size largest; in KB)
        GLint atiFree[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VBO_FREE_MEMORY_ATI, atiFree);
        if (atiFree[0] > 0)
        {
            const uint64_t freeKb = static_cast<uint64_t>(atiFree[0]);
            totalBytes = freeKb * 1024; // report free as "total" for display; used unknown
            usedBytes = 0;
            GPUMetricsProvider::SetVram(usedBytes, totalBytes);
            return;
        }

        // Neither extension available; do not overwrite previous values with zero
#else
        (void)0;
#endif
    }

} // namespace Limitless
