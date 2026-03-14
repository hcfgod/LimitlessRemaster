#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace Limitless
{
    /// Returns a lowercased copy of the input string.
    inline std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
}
