#pragma once

#include "Core/Error.h"

#include <string>

namespace Limitless::Assets
{
    struct ParsedShaderStages
    {
        std::string Name;
        std::string Vertex;
        std::string Fragment;
    };

    // Parses a combined GLSL file containing:
    //   #type vertex
    //   ...
    //   #type fragment
    //   ...
    Result<ParsedShaderStages> ParseCombinedGlsl(
        const std::string& key,
        const std::string& resolvedPath,
        const std::string& fileText,
        const std::string& nameOverride = {});

    // Optional: runs build-time validation / reflection steps for the active graphics API.
    // For OpenGL (with shaderc enabled), this compiles to SPIR-V for validation and logs a resource summary.
    Result<ParsedShaderStages> PrepareShaderStagesForActiveGraphicsAPI(ParsedShaderStages parsed, const std::string& debugPath);
}

