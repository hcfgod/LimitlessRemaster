#include "Assets/TilesetAsset.h"

#include "Assets/AssetDatabase.h"
#include "Assets/GeneratedAssetRuntimeRegistry.h"
#include "Assets/ImageDecode.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace Limitless::Assets
{
    namespace
    {
        double ComputeLumaDifferenceAtPixels(const DecodedImageRGBA8& image,
                                             int32_t leftPixelX,
                                             int32_t leftPixelY,
                                             int32_t rightPixelX,
                                             int32_t rightPixelY)
        {
            if (leftPixelX < 0 || leftPixelY < 0 || rightPixelX < 0 || rightPixelY < 0)
                return 0.0;
            if (leftPixelX >= static_cast<int32_t>(image.Width) || rightPixelX >= static_cast<int32_t>(image.Width))
                return 0.0;
            if (leftPixelY >= static_cast<int32_t>(image.Height) || rightPixelY >= static_cast<int32_t>(image.Height))
                return 0.0;

            const auto loadLuma = [&](int32_t pixelX, int32_t pixelY) -> double {
                const size_t pixelIndex = (static_cast<size_t>(pixelY) * static_cast<size_t>(image.Width) + static_cast<size_t>(pixelX)) * 4u;
                if (pixelIndex + 2u >= image.Pixels.size())
                    return 0.0;
                const double red = static_cast<double>(image.Pixels[pixelIndex + 0u]);
                const double green = static_cast<double>(image.Pixels[pixelIndex + 1u]);
                const double blue = static_cast<double>(image.Pixels[pixelIndex + 2u]);
                return red * 0.299 + green * 0.587 + blue * 0.114;
            };

            return std::abs(loadLuma(leftPixelX, leftPixelY) - loadLuma(rightPixelX, rightPixelY));
        }

        std::vector<int32_t> BuildAxisTileSizeCandidates(int32_t axisPixelCount, int32_t preferredAxisTileSizePixels)
        {
            static constexpr std::array<int32_t, 17> commonTileSizes = {
                8, 12, 16, 24, 32, 40, 48, 56, 64, 72, 80, 96, 112, 128, 160, 192, 256
            };

            std::vector<int32_t> candidates;
            candidates.reserve(commonTileSizes.size() + 2u);
            for (const int32_t tileSize : commonTileSizes)
            {
                if (tileSize <= 1 || tileSize > axisPixelCount)
                    continue;
                if (axisPixelCount % tileSize != 0)
                    continue;
                if (axisPixelCount / tileSize < 2)
                    continue;
                candidates.push_back(tileSize);
            }

            if (preferredAxisTileSizePixels > 1 &&
                preferredAxisTileSizePixels <= axisPixelCount &&
                axisPixelCount % preferredAxisTileSizePixels == 0 &&
                axisPixelCount / preferredAxisTileSizePixels >= 2)
            {
                if (std::find(candidates.begin(), candidates.end(), preferredAxisTileSizePixels) == candidates.end())
                    candidates.push_back(preferredAxisTileSizePixels);
            }

            if (candidates.empty())
            {
                const int32_t maxProbe = std::min(256, axisPixelCount);
                for (int32_t tileSize = 2; tileSize <= maxProbe; ++tileSize)
                {
                    if (axisPixelCount % tileSize != 0)
                        continue;
                    if (axisPixelCount / tileSize < 2)
                        continue;
                    candidates.push_back(tileSize);
                }
            }

            if (candidates.empty())
                candidates.push_back(std::max(1, axisPixelCount));

            std::sort(candidates.begin(), candidates.end());
            return candidates;
        }

        int32_t SelectTileSizeForAxisByEdgeContrast(const DecodedImageRGBA8& image,
                                                    bool evaluateVerticalEdges,
                                                    int32_t preferredAxisTileSizePixels)
        {
            const int32_t axisPixelCount = evaluateVerticalEdges
                ? static_cast<int32_t>(image.Width)
                : static_cast<int32_t>(image.Height);
            if (axisPixelCount <= 1)
                return std::max(1, preferredAxisTileSizePixels);

            const std::vector<int32_t> candidates = BuildAxisTileSizeCandidates(axisPixelCount, preferredAxisTileSizePixels);
            const auto chooseClosestCandidateToPreference = [&](int32_t preference) {
                int32_t closest = candidates.front();
                for (const int32_t candidate : candidates)
                {
                    if (std::abs(candidate - preference) < std::abs(closest - preference))
                        closest = candidate;
                }
                return closest;
            };

            const int32_t sampleStep = std::max(1, evaluateVerticalEdges
                ? static_cast<int32_t>(image.Height / 256u)
                : static_cast<int32_t>(image.Width / 256u));

            struct CandidateScore
            {
                int32_t TileSize = 0;
                double ContrastRatio = 0.0;
                double ContrastDelta = 0.0;
                int32_t TileCount = 0;
            };

            CandidateScore best{};
            best.ContrastRatio = -std::numeric_limits<double>::infinity();
            best.ContrastDelta = -std::numeric_limits<double>::infinity();

            for (const int32_t candidateTileSize : candidates)
            {
                const int32_t tileCount = axisPixelCount / candidateTileSize;
                if (tileCount < 2)
                    continue;

                double boundaryDifferenceSum = 0.0;
                int64_t boundarySamples = 0;
                for (int32_t boundaryIndex = 1; boundaryIndex < tileCount; ++boundaryIndex)
                {
                    const int32_t boundaryPixel = boundaryIndex * candidateTileSize;
                    if (evaluateVerticalEdges)
                    {
                        for (int32_t sampleY = 0; sampleY < static_cast<int32_t>(image.Height); sampleY += sampleStep)
                        {
                            boundaryDifferenceSum += ComputeLumaDifferenceAtPixels(image, boundaryPixel - 1, sampleY, boundaryPixel, sampleY);
                            ++boundarySamples;
                        }
                    }
                    else
                    {
                        for (int32_t sampleX = 0; sampleX < static_cast<int32_t>(image.Width); sampleX += sampleStep)
                        {
                            boundaryDifferenceSum += ComputeLumaDifferenceAtPixels(image, sampleX, boundaryPixel - 1, sampleX, boundaryPixel);
                            ++boundarySamples;
                        }
                    }
                }
                if (boundarySamples <= 0)
                    continue;

                double interiorDifferenceSum = 0.0;
                int64_t interiorSamples = 0;
                for (int32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
                {
                    const int32_t interiorPixel = tileIndex * candidateTileSize + (candidateTileSize / 2);
                    if (interiorPixel <= 0 || interiorPixel >= axisPixelCount)
                        continue;

                    if (evaluateVerticalEdges)
                    {
                        for (int32_t sampleY = 0; sampleY < static_cast<int32_t>(image.Height); sampleY += sampleStep)
                        {
                            interiorDifferenceSum += ComputeLumaDifferenceAtPixels(image, interiorPixel - 1, sampleY, interiorPixel, sampleY);
                            ++interiorSamples;
                        }
                    }
                    else
                    {
                        for (int32_t sampleX = 0; sampleX < static_cast<int32_t>(image.Width); sampleX += sampleStep)
                        {
                            interiorDifferenceSum += ComputeLumaDifferenceAtPixels(image, sampleX, interiorPixel - 1, sampleX, interiorPixel);
                            ++interiorSamples;
                        }
                    }
                }
                if (interiorSamples <= 0)
                    continue;

                const double boundaryMean = boundaryDifferenceSum / static_cast<double>(boundarySamples);
                const double interiorMean = interiorDifferenceSum / static_cast<double>(interiorSamples);
                const double contrastDelta = boundaryMean - interiorMean;
                const double contrastRatio = boundaryMean / std::max(1e-5, interiorMean);

                const bool preferCandidate = (contrastRatio > best.ContrastRatio + 0.005) ||
                    (std::abs(contrastRatio - best.ContrastRatio) <= 0.005 && contrastDelta > best.ContrastDelta + 0.1) ||
                    (std::abs(contrastRatio - best.ContrastRatio) <= 0.005 &&
                     std::abs(contrastDelta - best.ContrastDelta) <= 0.1 &&
                     std::abs(candidateTileSize - preferredAxisTileSizePixels) < std::abs(best.TileSize - preferredAxisTileSizePixels));

                if (preferCandidate)
                {
                    best.TileSize = candidateTileSize;
                    best.ContrastRatio = contrastRatio;
                    best.ContrastDelta = contrastDelta;
                    best.TileCount = tileCount;
                }
            }

            if (best.TileSize <= 0)
                return chooseClosestCandidateToPreference(preferredAxisTileSizePixels);

            // When signal is weak, keep a deterministic fallback close to the caller's preference.
            if (best.ContrastRatio < 1.02 && best.ContrastDelta < 1.0)
                return chooseClosestCandidateToPreference(preferredAxisTileSizePixels);

            return best.TileSize;
        }

        bool CellContainsVisiblePixelsByAlpha(const DecodedImageRGBA8& image,
                                              int32_t cellLeftPixels,
                                              int32_t cellTopPixels,
                                              int32_t cellWidthPixels,
                                              int32_t cellHeightPixels)
        {
            const int32_t imageWidth = static_cast<int32_t>(image.Width);
            const int32_t imageHeight = static_cast<int32_t>(image.Height);
            if (cellLeftPixels < 0 || cellTopPixels < 0 || cellWidthPixels <= 0 || cellHeightPixels <= 0)
                return false;
            if (cellLeftPixels + cellWidthPixels > imageWidth || cellTopPixels + cellHeightPixels > imageHeight)
                return false;

            static constexpr uint8_t visibleAlphaThreshold = 8u;
            for (int32_t pixelY = cellTopPixels; pixelY < (cellTopPixels + cellHeightPixels); ++pixelY)
            {
                for (int32_t pixelX = cellLeftPixels; pixelX < (cellLeftPixels + cellWidthPixels); ++pixelX)
                {
                    const size_t pixelIndex = (static_cast<size_t>(pixelY) * static_cast<size_t>(image.Width) + static_cast<size_t>(pixelX)) * 4u;
                    if (pixelIndex + 3u >= image.Pixels.size())
                        continue;
                    if (image.Pixels[pixelIndex + 3u] > visibleAlphaThreshold)
                        return true;
                }
            }

            return false;
        }

        std::vector<glm::ivec4> BuildCompactedExplicitTileRectsPixels(const DecodedImageRGBA8& image,
                                                                       const glm::ivec2& tileSizePixels)
        {
            const int32_t tileWidthPixels = std::max(1, tileSizePixels.x);
            const int32_t tileHeightPixels = std::max(1, tileSizePixels.y);
            if (tileWidthPixels <= 0 || tileHeightPixels <= 0)
                return {};
            if (image.Width == 0u || image.Height == 0u)
                return {};

            const int32_t imageWidthPixels = static_cast<int32_t>(image.Width);
            const int32_t imageHeightPixels = static_cast<int32_t>(image.Height);
            if (imageWidthPixels % tileWidthPixels != 0 || imageHeightPixels % tileHeightPixels != 0)
                return {};

            const int32_t columnCount = imageWidthPixels / tileWidthPixels;
            const int32_t rowCount = imageHeightPixels / tileHeightPixels;
            const int32_t totalCellCount = columnCount * rowCount;
            if (columnCount <= 0 || rowCount <= 0 || totalCellCount <= 0)
                return {};

            std::vector<glm::ivec4> explicitRectsPixels;
            explicitRectsPixels.reserve(static_cast<size_t>(totalCellCount));
            for (int32_t rowTopIndex = 0; rowTopIndex < rowCount; ++rowTopIndex)
            {
                const int32_t topPixels = rowTopIndex * tileHeightPixels;
                for (int32_t columnIndex = 0; columnIndex < columnCount; ++columnIndex)
                {
                    const int32_t leftPixels = columnIndex * tileWidthPixels;
                    if (!CellContainsVisiblePixelsByAlpha(image, leftPixels, topPixels, tileWidthPixels, tileHeightPixels))
                        continue;

                    // Runtime UV conversion expects pixel-space Y measured from the texture bottom.
                    const int32_t bottomPixels = imageHeightPixels - (topPixels + tileHeightPixels);
                    explicitRectsPixels.emplace_back(leftPixels, bottomPixels, tileWidthPixels, tileHeightPixels);
                }
            }

            if (explicitRectsPixels.empty())
                return {};
            if (static_cast<int32_t>(explicitRectsPixels.size()) >= totalCellCount)
                return {};
            return explicitRectsPixels;
        }

        glm::ivec2 ParseClampedIvec2OrDefault(const nlohmann::json& root,
                                              const char* fieldName,
                                              const glm::ivec2& defaultValue,
                                              int32_t minValue)
        {
            if (!fieldName || !root.contains(fieldName))
                return defaultValue;

            const auto& value = root[fieldName];
            if (value.is_number_integer())
            {
                const int32_t scalar = std::max(minValue, value.get<int32_t>());
                return glm::ivec2(scalar, scalar);
            }
            if (value.is_array() && value.size() >= 2)
            {
                return glm::ivec2(
                    std::max(minValue, value[0].get<int32_t>()),
                    std::max(minValue, value[1].get<int32_t>()));
            }

            return defaultValue;
        }

        void ParseExplicitTileRects(const nlohmann::json& root, TilesetAssetDefinition& definition)
        {
            const nlohmann::json* tilesJson = nullptr;
            if (root.contains("ExplicitTileRectsPixels") && root["ExplicitTileRectsPixels"].is_array())
                tilesJson = &root["ExplicitTileRectsPixels"];
            else if (root.contains("TilesetExplicitTileRectsPixels") && root["TilesetExplicitTileRectsPixels"].is_array())
                tilesJson = &root["TilesetExplicitTileRectsPixels"];
            else if (root.contains("Tiles") && root["Tiles"].is_array())
                tilesJson = &root["Tiles"];

            if (!tilesJson)
                return;

            definition.ExplicitTileRectsPixels.clear();
            definition.ExplicitTileRectsPixels.reserve(tilesJson->size());
            for (const auto& tileJson : *tilesJson)
            {
                std::array<int32_t, 4> rect{ 0, 0, 0, 0 };
                bool hasRect = false;

                if (tileJson.is_array() && tileJson.size() >= 4)
                {
                    rect[0] = tileJson[0].get<int32_t>();
                    rect[1] = tileJson[1].get<int32_t>();
                    rect[2] = tileJson[2].get<int32_t>();
                    rect[3] = tileJson[3].get<int32_t>();
                    hasRect = true;
                }
                else if (tileJson.is_object())
                {
                    if (tileJson.contains("RectPixels") && tileJson["RectPixels"].is_array() && tileJson["RectPixels"].size() >= 4)
                    {
                        const auto& value = tileJson["RectPixels"];
                        rect[0] = value[0].get<int32_t>();
                        rect[1] = value[1].get<int32_t>();
                        rect[2] = value[2].get<int32_t>();
                        rect[3] = value[3].get<int32_t>();
                        hasRect = true;
                    }
                    else if (tileJson.contains("Rect") && tileJson["Rect"].is_array() && tileJson["Rect"].size() >= 4)
                    {
                        const auto& value = tileJson["Rect"];
                        rect[0] = value[0].get<int32_t>();
                        rect[1] = value[1].get<int32_t>();
                        rect[2] = value[2].get<int32_t>();
                        rect[3] = value[3].get<int32_t>();
                        hasRect = true;
                    }
                }

                if (!hasRect)
                    continue;
                if (rect[2] <= 0 || rect[3] <= 0)
                    continue;

                definition.ExplicitTileRectsPixels.emplace_back(
                    std::max(0, rect[0]),
                    std::max(0, rect[1]),
                    std::max(1, rect[2]),
                    std::max(1, rect[3]));
            }
        }
    }

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

        nlohmann::json json;
        if (IsGeneratedAssetKey(tilesetAssetKey, AssetType::Tileset))
        {
            const auto textResult = GeneratedAssetRuntimeRegistry::GetInstance().LoadText(tilesetAssetKey);
            if (textResult.IsFailure())
            {
                if (outError)
                    *outError = textResult.GetError().GetErrorMessage();
                return false;
            }

            try
            {
                json = nlohmann::json::parse(textResult.GetValue());
            }
            catch (const std::exception& exception)
            {
                if (outError)
                    *outError = "Failed to parse generated tileset JSON: " + std::string(exception.what());
                return false;
            }
        }
        else
        {
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
        definition.MarginPixels = ParseClampedIvec2OrDefault(
            json,
            "MarginPixels",
            ParseClampedIvec2OrDefault(json, "Margin", definition.MarginPixels, 0),
            0);
        definition.SpacingPixels = ParseClampedIvec2OrDefault(
            json,
            "SpacingPixels",
            ParseClampedIvec2OrDefault(json, "Spacing", definition.SpacingPixels, 0),
            0);
        ParseExplicitTileRects(json, definition);

        outDefinition = std::move(definition);
        return true;
    }

    bool TryEnsureTilesetAssetForTextureKey(const std::string& textureAssetKey,
                                            const glm::ivec2& preferredTileSizePixels,
                                            std::string& outTilesetAssetKey,
                                            std::string* outError)
    {
        outTilesetAssetKey.clear();
        if (textureAssetKey.empty())
        {
            if (outError)
                *outError = "Texture asset key is empty.";
            return false;
        }
        if (textureAssetKey.rfind("Assets/", 0) != 0 && textureAssetKey.rfind("Assets\\", 0) != 0)
        {
            if (outError)
                *outError = "Texture asset key must be under Assets/.";
            return false;
        }

        const std::filesystem::path textureKeyPath(textureAssetKey);
        const std::filesystem::path tilesetKeyPath = textureKeyPath.parent_path() /
            (textureKeyPath.stem().string() + ".tileset.json");
        outTilesetAssetKey = tilesetKeyPath.generic_string();

        glm::ivec2 detectedTileSizePixels = glm::ivec2(
            std::max(1, preferredTileSizePixels.x),
            std::max(1, preferredTileSizePixels.y));
        std::vector<glm::ivec4> detectedCompactedTileRectsPixels;
        const auto texturePathResult = ResolveAssetKeyToPath(textureAssetKey);
        if (texturePathResult.IsSuccess())
        {
            const auto decodeResult = DecodeToRGBA8(texturePathResult.GetValue().string(), false);
            if (decodeResult.IsSuccess())
            {
                const DecodedImageRGBA8& decodedImage = decodeResult.GetValue();
                if (decodedImage.Width > 0u && decodedImage.Height > 0u)
                {
                    detectedTileSizePixels = glm::ivec2(
                        std::max(1, SelectTileSizeForAxisByEdgeContrast(decodedImage, true, detectedTileSizePixels.x)),
                        std::max(1, SelectTileSizeForAxisByEdgeContrast(decodedImage, false, detectedTileSizePixels.y)));
                    detectedCompactedTileRectsPixels = BuildCompactedExplicitTileRectsPixels(decodedImage, detectedTileSizePixels);
                }
            }
        }

        const auto tilesetPathResult = ResolveAssetKeyToPath(outTilesetAssetKey);
        if (tilesetPathResult.IsFailure())
        {
            if (outError)
                *outError = tilesetPathResult.GetError().GetErrorMessage();
            outTilesetAssetKey.clear();
            return false;
        }

        const std::filesystem::path tilesetPath = tilesetPathResult.GetValue();
        auto makeExplicitRectsJson = [](const std::vector<glm::ivec4>& explicitRectsPixels) {
            nlohmann::json explicitRectsJson = nlohmann::json::array();
            for (const glm::ivec4& rectPixels : explicitRectsPixels)
                explicitRectsJson.push_back({ rectPixels.x, rectPixels.y, rectPixels.z, rectPixels.w });
            return explicitRectsJson;
        };

        std::error_code fileError;
        if (!std::filesystem::exists(tilesetPath, fileError))
        {
            std::filesystem::create_directories(tilesetPath.parent_path(), fileError);
            if (fileError)
            {
                if (outError)
                    *outError = "Failed to create tileset directory: " + fileError.message();
                outTilesetAssetKey.clear();
                return false;
            }

            nlohmann::json defaultDefinition = {
                { "TextureKey", textureAssetKey },
                { "TileSizePixels", {
                    std::max(1, detectedTileSizePixels.x),
                    std::max(1, detectedTileSizePixels.y)
                } },
                { "MarginPixels", { 0, 0 } },
                { "SpacingPixels", { 0, 0 } }
            };
            if (!detectedCompactedTileRectsPixels.empty())
                defaultDefinition["ExplicitTileRectsPixels"] = makeExplicitRectsJson(detectedCompactedTileRectsPixels);

            std::ofstream output(tilesetPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                if (outError)
                    *outError = "Failed to create tileset file: " + tilesetPath.string();
                outTilesetAssetKey.clear();
                return false;
            }
            output << defaultDefinition.dump(4);
        }
        else if (!detectedCompactedTileRectsPixels.empty())
        {
            nlohmann::json existingDefinition{};
            bool hasValidExistingDefinition = false;
            {
                std::ifstream input(tilesetPath, std::ios::in | std::ios::binary);
                if (input.is_open())
                {
                    try
                    {
                        input >> existingDefinition;
                        hasValidExistingDefinition = existingDefinition.is_object();
                    }
                    catch (...)
                    {
                        hasValidExistingDefinition = false;
                    }
                }
            }

            if (hasValidExistingDefinition)
            {
                TilesetAssetDefinition existingParsedDefinition{};
                ParseExplicitTileRects(existingDefinition, existingParsedDefinition);
                if (existingParsedDefinition.ExplicitTileRectsPixels.empty())
                {
                    existingDefinition["ExplicitTileRectsPixels"] = makeExplicitRectsJson(detectedCompactedTileRectsPixels);
                    if (!existingDefinition.contains("TextureKey") || !existingDefinition["TextureKey"].is_string())
                        existingDefinition["TextureKey"] = textureAssetKey;
                    if (!existingDefinition.contains("TileSizePixels") || !existingDefinition["TileSizePixels"].is_array() || existingDefinition["TileSizePixels"].size() < 2)
                    {
                        existingDefinition["TileSizePixels"] = {
                            std::max(1, detectedTileSizePixels.x),
                            std::max(1, detectedTileSizePixels.y)
                        };
                    }

                    std::ofstream output(tilesetPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (output.is_open())
                        output << existingDefinition.dump(4);
                }
            }
        }

        const auto importResult = AssetDatabase::GetInstance().ImportOrUpdate(
            outTilesetAssetKey,
            AssetType::Tileset,
            nlohmann::json::object());
        if (importResult.IsFailure())
        {
            if (outError)
                *outError = importResult.GetError().GetErrorMessage();
            outTilesetAssetKey.clear();
            return false;
        }

        return true;
    }
}
