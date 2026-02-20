#include "Assets/TileAsset.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Limitless::Assets
{
    // -------------------------------------------------------------------------
    // Collider type serialization helpers
    // -------------------------------------------------------------------------

    static const char* ColliderTypeToString(TileAssetData::ColliderType type)
    {
        switch (type)
        {
            case TileAssetData::ColliderType::Grid:   return "Grid";
            case TileAssetData::ColliderType::Sprite: return "Sprite";
            default:                                  return "None";
        }
    }

    static TileAssetData::ColliderType ColliderTypeFromString(const std::string& str)
    {
        if (str == "Grid")   return TileAssetData::ColliderType::Grid;
        if (str == "Sprite") return TileAssetData::ColliderType::Sprite;
        return TileAssetData::ColliderType::None;
    }

    // -------------------------------------------------------------------------
    // JSON conversion
    // -------------------------------------------------------------------------

    static nlohmann::json TileAssetDataToJson(const TileAssetData& data)
    {
        nlohmann::json j;
        j["SpriteTextureKey"] = data.SpriteTextureKey;
        j["SubSpriteIndex"]   = data.SubSpriteIndex;
        j["Color"]            = { data.Color.r, data.Color.g, data.Color.b, data.Color.a };
        j["ColliderType"]     = ColliderTypeToString(data.Collider);
        return j;
    }

    static TileAssetData TileAssetDataFromJson(const nlohmann::json& j)
    {
        TileAssetData data;

        if (j.contains("SpriteTextureKey") && j["SpriteTextureKey"].is_string())
            data.SpriteTextureKey = j["SpriteTextureKey"].get<std::string>();

        if (j.contains("SubSpriteIndex") && j["SubSpriteIndex"].is_number_integer())
            data.SubSpriteIndex = j["SubSpriteIndex"].get<int32_t>();

        if (j.contains("Color") && j["Color"].is_array() && j["Color"].size() >= 4)
        {
            data.Color.r = j["Color"][0].get<float>();
            data.Color.g = j["Color"][1].get<float>();
            data.Color.b = j["Color"][2].get<float>();
            data.Color.a = j["Color"][3].get<float>();
        }

        if (j.contains("ColliderType") && j["ColliderType"].is_string())
            data.Collider = ColliderTypeFromString(j["ColliderType"].get<std::string>());

        return data;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    Result<TileAssetData> LoadTileAssetData(const std::string& tileAssetKey)
    {
        if (tileAssetKey.empty())
            return Result<TileAssetData>(ErrorCode::InvalidArgument, "Tile asset key is empty.");

        const auto pathResult = ResolveAssetKeyToPath(tileAssetKey);
        if (pathResult.IsFailure())
            return Result<TileAssetData>(pathResult.GetError());

        std::ifstream input(pathResult.GetValue(), std::ios::in | std::ios::binary);
        if (!input.is_open())
            return Result<TileAssetData>(ErrorCode::FileNotFound, "Failed to open tile asset: " + tileAssetKey);

        try
        {
            nlohmann::json j;
            input >> j;
            return Result<TileAssetData>(TileAssetDataFromJson(j));
        }
        catch (const std::exception& e)
        {
            return Result<TileAssetData>(ErrorCode::FileCorrupted, "Failed to parse tile JSON: " + std::string(e.what()));
        }
    }

    Result<void> SaveTileAssetData(const std::string& tileAssetKey, const TileAssetData& data)
    {
        if (tileAssetKey.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Tile asset key is empty.");

        const auto pathResult = ResolveAssetKeyToPath(tileAssetKey);
        if (pathResult.IsFailure())
            return Result<void>(pathResult.GetError());

        return WriteTileAssetFile(pathResult.GetValue(), data);
    }

    Result<void> WriteTileAssetFile(const std::filesystem::path& absolutePath, const TileAssetData& data)
    {
        try
        {
            if (absolutePath.has_parent_path())
                std::filesystem::create_directories(absolutePath.parent_path());

            std::ofstream output(absolutePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return Result<void>(ErrorCode::FileAccessDenied, "Failed to open for writing: " + absolutePath.string());

            output << TileAssetDataToJson(data).dump(4);
            output.flush();
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("Failed to write tile asset: ") + e.what());
        }

        return Result<void>();
    }
}
