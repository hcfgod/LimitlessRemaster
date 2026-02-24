#pragma once

namespace Limitless {

    /**
     * Query current GPU VRAM from OpenGL extensions (NVIDIA NVX, AMD ATI)
     * and update GPUMetricsProvider. Call only when an OpenGL context is current
     * (e.g. from render thread or after ProcessCommands).
     */
    void UpdateGPUMetricsFromOpenGL();

} // namespace Limitless
