#pragma once

#include "Core/Error.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Limitless::Assets::Cooking
{
    struct CookedShaderStages
    {
        std::string Name;
        std::string Vertex;
        std::string Fragment;
    };

    Result<std::vector<uint8_t>> CookShaderStagesToBytes(const CookedShaderStages& stages);
    Result<CookedShaderStages> ParseCookedShaderStages(const uint8_t* bytes, size_t byteCount);
}

