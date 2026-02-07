#pragma once

#include <cstdint>
#include <string>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetType
    // Minimal "engine types" set. Extend as importers are added.
    // -----------------------------------------------------------------------------
    enum class AssetType : std::uint32_t
    {
        Unknown = 0,
        Texture2D = 1,
        Shader = 2,
        Material = 3,
        Mesh = 4,
        Scene = 5
    };

    [[nodiscard]] const char* ToString(AssetType type);
    [[nodiscard]] AssetType AssetTypeFromString(const std::string& s);
}

