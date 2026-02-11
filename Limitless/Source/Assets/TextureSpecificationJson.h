#pragma once

#include "Core/Error.h"
#include "Graphics/Texture.h"

#include <nlohmann/json.hpp>

namespace Limitless::Assets
{
    // Converts importer settings JSON -> TextureSpecification.
    // Missing fields fall back to defaults.
    TextureSpecification TextureSpecificationFromImporterSettingsJson(const nlohmann::json& j);

    // Returns true if `s` is exactly the default-constructed spec.
    bool IsDefaultTextureSpecification(const TextureSpecification& s);
}

