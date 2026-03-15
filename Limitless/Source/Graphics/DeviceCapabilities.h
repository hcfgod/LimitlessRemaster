#pragma once

#include <cstdint>

namespace Limitless
{
    struct GfxDeviceCapabilities
    {
        int32_t MaxTextureSlots = 16;
        int32_t MaxColorAttachments = 8;
        int32_t MaxUniformBufferBindings = 12;
        int32_t MaxTextureSize = 4096;
        int32_t MaxFramebufferWidth = 4096;
        int32_t MaxFramebufferHeight = 4096;
        int32_t MaxVertexAttributes = 16;

        bool SupportsComputeShaders = false;
        bool SupportsSPIRV = false;
        bool SupportsSharedResourceContext = false;
        bool SupportsMultisample = false;
        bool SupportsAnisotropicFiltering = false;
        bool SupportsInstancing = true;
        bool SupportsBaseVertex = true;
    };
}
