#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Limitless::ShaderCompilation
{
    enum class ShaderStage : uint8_t
    {
        Vertex = 0,
        Fragment = 1,
        Compute = 2
    };

    /**
     * @brief Compile GLSL into SPIR-V words (uint32_t stream).
     *
     * This is a CPU-only operation and is safe to run off the render thread.
     * On platforms/configurations where shaderc is not available, this will
     * return a NotSupported error.
     */
    Result<std::vector<uint32_t>> CompileGlslToSpirv(
        ShaderStage stage,
        std::string_view glslSource,
        std::string_view debugName,
        std::string_view entryPoint = "main");

    /**
     * @brief Convert SPIR-V to GLSL using SPIRV-Cross.
     *
     * This is primarily useful for keeping a single compilation/validation
     * pipeline even when the runtime API consumes GLSL (OpenGL).
     */
    Result<std::string> TranspileSpirvToGlsl(
        const std::vector<uint32_t>& spirvWords,
        int glslVersion,
        bool emitLineDirectives,
        std::string_view debugName);

    /**
     * @brief Debug-only helper: log a high-level SPIR-V resource summary.
     *
     * This exists so we can prove the SPIR-V toolchain is being exercised even
     * when the runtime backend consumes native GLSL (OpenGL).
     *
     * It never throws and never fails the load path.
     */
    void DebugLogSpirvResourceSummary(const std::vector<uint32_t>& spirvWords, std::string_view debugName);
}

