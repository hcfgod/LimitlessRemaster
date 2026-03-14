#include "Assets/TilePaletteAsset.h"
#include "Assets/TileAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporterVersion.h"
#include "Assets/AssetPaths.h"
#include "Assets/SpriteImportSettings.h"
#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace Limitless::Assets
{
    // -------------------------------------------------------------------------
    // JSON helpers
    // -------------------------------------------------------------------------

    static nlohmann::json TilePaletteDataToJson(const TilePaletteData& data)
    {
        nlohmann::json j;
        j["GridSize"] = { data.GridSize.x, data.GridSize.y };
        j["SourceTextureKey"] = data.SourceTextureKey;

        nlohmann::json tiles = nlohmann::json::array();
        for (const auto& key : data.TileAssetKeys)
            tiles.push_back(key);
        j["TileAssetKeys"] = std::move(tiles);

        return j;
    }

    static TilePaletteData TilePaletteDataFromJson(const nlohmann::json& j)
    {
        TilePaletteData data;

        if (j.contains("GridSize") && j["GridSize"].is_array() && j["GridSize"].size() >= 2)
        {
            data.GridSize.x = std::max(0, j["GridSize"][0].get<int32_t>());
            data.GridSize.y = std::max(0, j["GridSize"][1].get<int32_t>());
        }

        if (j.contains("SourceTextureKey") && j["SourceTextureKey"].is_string())
            data.SourceTextureKey = j["SourceTextureKey"].get<std::string>();

        if (j.contains("TileAssetKeys") && j["TileAssetKeys"].is_array())
        {
            for (const auto& entry : j["TileAssetKeys"])
            {
                if (entry.is_string())
                    data.TileAssetKeys.push_back(entry.get<std::string>());
                else
                    data.TileAssetKeys.emplace_back();
            }
        }

        return data;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    Result<TilePaletteData> LoadTilePaletteData(const std::string& paletteAssetKey)
    {
        if (paletteAssetKey.empty())
            return Result<TilePaletteData>(ErrorCode::InvalidArgument, "Palette asset key is empty.");

        const auto pathResult = ResolveAssetKeyToPath(paletteAssetKey);
        if (pathResult.IsFailure())
            return Result<TilePaletteData>(pathResult.GetError());

        std::ifstream input(pathResult.GetValue(), std::ios::in | std::ios::binary);
        if (!input.is_open())
            return Result<TilePaletteData>(ErrorCode::FileNotFound,
                "Failed to open palette asset: " + paletteAssetKey);

        try
        {
            nlohmann::json j;
            input >> j;
            return Result<TilePaletteData>(TilePaletteDataFromJson(j));
        }
        catch (const std::exception& e)
        {
            return Result<TilePaletteData>(ErrorCode::FileCorrupted,
                "Failed to parse palette JSON: " + std::string(e.what()));
        }
    }

    Result<void> SaveTilePaletteData(const std::string& paletteAssetKey,
                                      const TilePaletteData& data)
    {
        if (paletteAssetKey.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Palette asset key is empty.");

        const auto pathResult = ResolveAssetKeyToPath(paletteAssetKey);
        if (pathResult.IsFailure())
            return Result<void>(pathResult.GetError());

        return WriteTilePaletteFile(pathResult.GetValue(), data);
    }

    Result<void> WriteTilePaletteFile(const std::filesystem::path& absolutePath,
                                       const TilePaletteData& data)
    {
        try
        {
            if (absolutePath.has_parent_path())
                std::filesystem::create_directories(absolutePath.parent_path());

            std::ofstream output(absolutePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return Result<void>(ErrorCode::FileAccessDenied,
                    "Failed to open for writing: " + absolutePath.string());

            output << TilePaletteDataToJson(data).dump(4);
            output.flush();
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied,
                std::string("Failed to write palette asset: ") + e.what());
        }

        return Result<void>();
    }

    Result<void> PopulatePaletteFromSpriteSheet(const std::string& paletteAssetKey,
                                                 const std::string& textureAssetKey,
                                                 TilePaletteData& outData)
    {
        if (paletteAssetKey.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Palette asset key is empty.");
        if (textureAssetKey.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Texture asset key is empty.");

        // Load the sprite import settings to get sub-sprite rects.
        const SpriteImportSettings spriteSettings = LoadSpriteImportSettings(textureAssetKey);
        if (spriteSettings.Mode != SpriteImportSettings::SpriteMode::Multiple || spriteSettings.SubSprites.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument,
                "Texture '" + textureAssetKey + "' is not sliced into multiple sub-sprites. "
                "Open the Sprite Editor and slice it first.");
        }

        // Resolve the palette's on-disk location to create a sibling tile folder.
        const auto palettePathResult = ResolveAssetKeyToPath(paletteAssetKey);
        if (palettePathResult.IsFailure())
            return Result<void>(palettePathResult.GetError());

        const std::filesystem::path palettePath = palettePathResult.GetValue();
        const std::filesystem::path paletteDir = palettePath.parent_path();

        // Derive a folder name from the palette filename (e.g., "MyPalette_Tiles").
        std::string paletteStem = palettePath.stem().stem().string(); // strip both .tilepalette and .json
        if (paletteStem.empty())
            paletteStem = "Palette";
        const std::filesystem::path tileFolder = paletteDir / (paletteStem + "_Tiles");
        std::filesystem::create_directories(tileFolder);

        // Resolve the project root so we can compute asset keys for the new tiles.
        const auto projectRootResult = FindProjectRootFromWorkingDirectory();
        if (projectRootResult.IsFailure())
            return Result<void>(projectRootResult.GetError());
        const std::filesystem::path projectRoot = projectRootResult.GetValue();

        // Derive texture stem for naming (e.g., "tileset" from "Assets/Textures/tileset.png").
        const std::filesystem::path textureKeyPath(textureAssetKey);
        const std::string textureStem = textureKeyPath.stem().string();

        const size_t subSpriteCount = spriteSettings.SubSprites.size();

        // Compute a grid layout that preserves the sprite sheet spatial order.
        // Try to figure out column count from the sub-sprite positions.
        int32_t columns = 1;
        if (subSpriteCount >= 2)
        {
            // Guess columns: count how many sub-sprites share the same Y as the first one.
            const int32_t firstY = spriteSettings.SubSprites[0].RectPixels.y;
            columns = 0;
            for (const auto& sub : spriteSettings.SubSprites)
            {
                if (sub.RectPixels.y == firstY)
                    ++columns;
                else
                    break;
            }
            if (columns < 1)
                columns = 1;
        }
        const int32_t rows = static_cast<int32_t>(
            std::ceil(static_cast<float>(subSpriteCount) / static_cast<float>(columns)));

        // Create tile assets and build the palette grid.
        std::vector<std::string> tileKeys;
        tileKeys.reserve(subSpriteCount);

        for (size_t i = 0; i < subSpriteCount; ++i)
        {
            const std::string tileName = textureStem + "_" + std::to_string(i);
            const std::filesystem::path tileFilePath = tileFolder / (tileName + ".tile.json");

            TileAssetData tileData;
            tileData.SpriteTextureKey = textureAssetKey;
            tileData.SubSpriteIndex = static_cast<int32_t>(i);
            tileData.Color = glm::vec4(1.0f);
            tileData.Collider = TileAssetData::ColliderType::Grid;

            const auto writeResult = WriteTileAssetFile(tileFilePath, tileData);
            if (writeResult.IsFailure())
            {
                LT_CORE_WARN("PopulatePaletteFromSpriteSheet: failed to write tile '{}': {}",
                             tileFilePath.string(), writeResult.GetError().GetErrorMessage());
                tileKeys.emplace_back();
                continue;
            }

            // Compute the asset key relative to the project root.
            const std::filesystem::path relPath =
                std::filesystem::relative(tileFilePath, projectRoot);
            const std::string tileAssetKey = relPath.generic_string();

            // Register the tile with the asset database.
            AssetDatabase::GetInstance().ImportOrUpdate(
                tileAssetKey,
                AssetType::Tile,
                nlohmann::json::object(),
                GetCurrentAssetImporterVersion(AssetType::Tile));

            tileKeys.push_back(tileAssetKey);
        }

        // Pad the tile key list to fill the full grid if needed.
        const size_t totalCells = static_cast<size_t>(columns) * static_cast<size_t>(rows);
        tileKeys.resize(totalCells);

        outData.GridSize = glm::ivec2(columns, rows);
        outData.TileAssetKeys = std::move(tileKeys);
        outData.SourceTextureKey = textureAssetKey;

        LT_CORE_INFO("TilePalette: populated {} tiles ({}x{}) from '{}'",
                      subSpriteCount, columns, rows, textureAssetKey);

        return Result<void>();
    }
}
