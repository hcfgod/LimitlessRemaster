#pragma once

#include "Core/Error.h"

#include <filesystem>
#include <string>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetPaths
    // Unity-style asset key resolution.
    //
    // In code we typically refer to assets by a project-relative key like:
    //   "Assets/Textures/Checker.ppm"
    //
    // At runtime, the working directory may be `Sandbox/`, `Build/.../Sandbox/`, etc.
    // We resolve keys to a real filesystem path by walking up from the current
    // working directory until we find a directory containing an `Assets/` folder.
    // -----------------------------------------------------------------------------

    // Returns the inferred project root directory that contains the `Assets/` folder.
    // This is discovered by walking up from std::filesystem::current_path().
    [[nodiscard]] Result<std::filesystem::path> FindProjectRootFromWorkingDirectory();

    // Resolve an asset key to an absolute filesystem path.
    //
    // Rules:
    // - If `assetKey` is absolute, it is returned as-is.
    // - If it starts with "Assets/" (Unity-style), we search for the project root.
    // - Otherwise it's treated as a path relative to the current working directory.
    [[nodiscard]] Result<std::filesystem::path> ResolveAssetKeyToPath(const std::string& assetKey);
}

