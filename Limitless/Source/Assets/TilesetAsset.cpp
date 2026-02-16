#include "Assets/TilesetAsset.h"

#include "Assets/AssetPaths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Limitless::Assets
{
    bool TryLoadTilesetAssetDefinition(const std::string& tilesetAssetKey,
                                       TilesetAssetDefinition& outDefinition,
                                       std::string* outError)
    {
        if (tilesetAssetKey.empty())
        {
            if (outError)
                *outError = "Tileset asset key is empty.";
            return false;
        }

        const auto resolvedPathResult = ResolveAssetKeyToPath(tilesetAssetKey);
        if (resolvedPathResult.IsFailure())
        {
            if (outError)
                *outError = resolvedPathResult.GetError().GetErrorMessage();
            return false;
        }

        const std::filesystem::path tilesetPath = resolvedPathResult.GetValue();
        std::ifstream input(tilesetPath, std::ios::in | std::ios::binary);
        if (!input.is_open())
        {
            if (outError)
                *outError = "Failed to open tileset file: " + tilesetPath.string();
            return false;
        }

        nlohmann::json json;
        try
        {
            input >> json;
        }
        catch (const std::exception& exception)
        {
            if (outError)
                *outError = "Failed to parse tileset JSON: " + std::string(exception.what());
            return false;
        }

        TilesetAssetDefinition definition{};
        if (json.contains("TextureKey") && json["TextureKey"].is_string())
            definition.TextureKey = json["TextureKey"].get<std::string>();
        else if (json.contains("Texture") && json["Texture"].is_object() && json["Texture"].contains("key") && json["Texture"]["key"].is_string())
            definition.TextureKey = json["Texture"]["key"].get<std::string>();

        if (json.contains("TileSizePixels") && json["TileSizePixels"].is_array() && json["TileSizePixels"].size() >= 2)
        {
            definition.TileSizePixels.x = std::max(1, json["TileSizePixels"][0].get<int32_t>());
            definition.TileSizePixels.y = std::max(1, json["TileSizePixels"][1].get<int32_t>());
        }

        outDefinition = std::move(definition);
        return true;
    }
}
