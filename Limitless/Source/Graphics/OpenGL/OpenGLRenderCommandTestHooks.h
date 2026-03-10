#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Limitless::OpenGLRenderCommandTestHooks
{
    struct RuntimeStateSnapshot
    {
        uint32_t Program = 0;
        uint32_t VertexArray = 0;
        uint32_t ActiveTextureUnit = 0;
        std::array<uint32_t, 32> BoundTexture2D{};
        uint32_t Renderer2DProgram = 0;
        int ViewProjectionLocation = -2;
        int ModelLocation = -2;
        bool HasViewProjection = false;
        std::size_t UniformProgramCount = 0;
        std::size_t UniformLocationCount = 0;
    };

    RuntimeStateSnapshot GetRuntimeStateSnapshot();
    void SetRuntimeStateSnapshot(const RuntimeStateSnapshot& snapshot);
    void ResetRuntimeState();
}
