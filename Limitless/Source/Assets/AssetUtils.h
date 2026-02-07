#pragma once

#include "Core/Error.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Limitless::Assets
{
    // Generate a random UUID-like GUID string (lower-case, no braces).
    [[nodiscard]] std::string GenerateGuid();

    // `.meta` utilities:
    // - Load existing GUID if `<assetPath>.meta` exists and is valid.
    // - Otherwise create it (and parent directories if needed).
    //
    // Note: This function may create files on disk.
    [[nodiscard]] Result<std::string> LoadOrCreateGuid(const std::string& assetPath, const nlohmann::json& extraMeta = {});

    // Update or create the `.meta` file to contain the given dependency GUIDs.
    // Existing unrelated fields are preserved when possible.
    Result<void> WriteDependencies(const std::string& assetPath, const std::vector<std::string>& dependencies);
}

