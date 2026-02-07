#pragma once

#include "Assets/AssetDatabase.h"
#include "Assets/AssetTypes.h"
#include "Core/Concurrency/AsyncIO.h"

#include <nlohmann/json.hpp>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetImporter<TAsset>
    // Type-trait layer that provides:
    // - AssetType mapping
    // - Settings type (importer settings)
    // - LoadAsync implementation
    //
    // Each asset type must provide a specialization.
    // -----------------------------------------------------------------------------
    template<typename TAsset>
    struct AssetImporter;
}

