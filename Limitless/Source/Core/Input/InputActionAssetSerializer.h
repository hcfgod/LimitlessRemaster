#pragma once

#include "Core/Error.h"

#include <memory>
#include <string>

namespace Limitless
{
    class InputActionAsset;

    class InputActionAssetSerializer final
    {
    public:
        // Unity-style: load/save an input action asset from JSON.
        // This is intentionally minimal and engine-owned (no generic asset pipeline required yet).
        static Result<std::shared_ptr<InputActionAsset>> LoadFromFile(const std::string& path);
        static Result<void> SaveToFile(const InputActionAsset& asset, const std::string& path);
    };
}

