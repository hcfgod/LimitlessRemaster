#include "Assets/SpriteImportSettings.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Limitless::Assets
{
    static std::mutex s_SpriteSettingsCacheMutex;
    static std::unordered_map<std::string, SpriteImportSettings> s_SpriteSettingsCache;
    // -------------------------------------------------------------------------
    // JSON helpers
    // -------------------------------------------------------------------------

    static std::string SpriteModeToString(SpriteImportSettings::SpriteMode mode)
    {
        switch (mode)
        {
            case SpriteImportSettings::SpriteMode::Multiple: return "multiple";
            default: return "single";
        }
    }

    static SpriteImportSettings::SpriteMode SpriteModeFromString(const std::string& str)
    {
        if (str == "multiple")
            return SpriteImportSettings::SpriteMode::Multiple;
        return SpriteImportSettings::SpriteMode::Single;
    }

    static std::string GetMetaPathForAssetKey(const std::string& textureAssetKey)
    {
        const auto pathResult = ResolveAssetKeyToPath(textureAssetKey);
        if (!pathResult.IsSuccess())
            return {};
        return pathResult.GetValue().string() + ".meta";
    }

    static nlohmann::json ReadMetaJson(const std::string& metaPath)
    {
        nlohmann::json j = nlohmann::json::object();
        if (metaPath.empty())
            return j;

        try
        {
            if (std::filesystem::exists(metaPath))
            {
                std::ifstream in(metaPath, std::ios::in | std::ios::binary);
                if (in.is_open())
                    in >> j;
            }
        }
        catch (const std::exception& e)
        {
            LT_CORE_WARN("SpriteImportSettings: failed to read meta '{}': {}", metaPath, e.what());
        }

        return j;
    }

    static Result<void> WriteMetaJson(const std::string& metaPath, const nlohmann::json& j)
    {
        if (metaPath.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Meta path is empty");

        try
        {
            const std::filesystem::path fsPath(metaPath);
            if (fsPath.has_parent_path())
                std::filesystem::create_directories(fsPath.parent_path());

            std::ofstream out(metaPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return Result<void>(ErrorCode::FileAccessDenied, "Failed to open meta file for writing: " + metaPath);

            out << j.dump(4);
            out.flush();
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("Failed to write meta file: ") + e.what());
        }

        return Result<void>();
    }

    static void ParseSpriteSettingsFromMetaJson(const nlohmann::json& j, SpriteImportSettings& settings)
    {
        if (j.contains("spriteMode") && j["spriteMode"].is_string())
            settings.Mode = SpriteModeFromString(j["spriteMode"].get<std::string>());

        if (j.contains("pixelsPerUnit") && j["pixelsPerUnit"].is_number())
            settings.PixelsPerUnit = std::max(0.01f, j["pixelsPerUnit"].get<float>());

        if (j.contains("subSprites") && j["subSprites"].is_array())
        {
            for (const auto& entry : j["subSprites"])
            {
                if (!entry.is_object())
                    continue;

                SpriteSubRect sub;

                if (entry.contains("name") && entry["name"].is_string())
                    sub.Name = entry["name"].get<std::string>();

                if (entry.contains("rect") && entry["rect"].is_array() && entry["rect"].size() >= 4)
                {
                    sub.RectPixels.x = entry["rect"][0].get<int>();
                    sub.RectPixels.y = entry["rect"][1].get<int>();
                    sub.RectPixels.z = entry["rect"][2].get<int>();
                    sub.RectPixels.w = entry["rect"][3].get<int>();
                }

                settings.SubSprites.push_back(std::move(sub));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    SpriteImportSettings LoadSpriteImportSettings(const std::string& textureAssetKey)
    {
        if (textureAssetKey.empty())
            return SpriteImportSettings{};

        {
            std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
            auto it = s_SpriteSettingsCache.find(textureAssetKey);
            if (it != s_SpriteSettingsCache.end())
                return it->second;
        }

        SpriteImportSettings settings;

        // Built/shipped runtime path: read texture meta payload from AssetBundle first.
        auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto metaTextResult = bundle.ReadAllTextByKey(textureAssetKey + ".meta");
            if (metaTextResult.IsSuccess())
            {
                try
                {
                    const nlohmann::json metaJson = nlohmann::json::parse(metaTextResult.GetValue());
                    ParseSpriteSettingsFromMetaJson(metaJson, settings);
                    std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
                    s_SpriteSettingsCache[textureAssetKey] = settings;
                    return settings;
                }
                catch (const std::exception& e)
                {
                    LT_CORE_WARN("SpriteImportSettings: failed to parse bundled meta for '{}': {}", textureAssetKey, e.what());
                }
            }
        }

        const std::string metaPath = GetMetaPathForAssetKey(textureAssetKey);
        if (metaPath.empty())
            return settings;

        const nlohmann::json j = ReadMetaJson(metaPath);
        ParseSpriteSettingsFromMetaJson(j, settings);

        {
            std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
            s_SpriteSettingsCache[textureAssetKey] = settings;
        }

        return settings;
    }

    Result<void> SaveSpriteImportSettings(const std::string& textureAssetKey,
                                          const SpriteImportSettings& settings)
    {
        const std::string metaPath = GetMetaPathForAssetKey(textureAssetKey);
        if (metaPath.empty())
            return Result<void>(ErrorCode::InvalidArgument, "Could not resolve meta path for: " + textureAssetKey);

        nlohmann::json j = ReadMetaJson(metaPath);

        j["spriteMode"] = SpriteModeToString(settings.Mode);
        j["pixelsPerUnit"] = settings.PixelsPerUnit;

        nlohmann::json subArray = nlohmann::json::array();
        for (const auto& sub : settings.SubSprites)
        {
            nlohmann::json entry;
            entry["name"] = sub.Name;
            entry["rect"] = { sub.RectPixels.x, sub.RectPixels.y, sub.RectPixels.z, sub.RectPixels.w };
            subArray.push_back(std::move(entry));
        }
        j["subSprites"] = std::move(subArray);

        auto result = WriteMetaJson(metaPath, j);
        if (result.IsSuccess())
        {
            std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
            s_SpriteSettingsCache[textureAssetKey] = settings;
        }
        return result;
    }

    void InvalidateSpriteImportSettingsCache()
    {
        std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
        s_SpriteSettingsCache.clear();
    }

    void InvalidateSpriteImportSettingsCacheEntry(const std::string& textureAssetKey)
    {
        std::lock_guard<std::mutex> lock(s_SpriteSettingsCacheMutex);
        s_SpriteSettingsCache.erase(textureAssetKey);
    }

    glm::vec4 ComputeSubSpriteUvs(const glm::ivec4& rectPixels,
                                   uint32_t textureWidth,
                                   uint32_t textureHeight,
                                   float insetTexels)
    {
        if (textureWidth == 0 || textureHeight == 0)
            return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

        const float invW = 1.0f / static_cast<float>(textureWidth);
        const float invH = 1.0f / static_cast<float>(textureHeight);
        const float safeInsetX = std::clamp(insetTexels, 0.0f, std::max(0.0f, (static_cast<float>(rectPixels.z) - 1.0f) * 0.5f));
        const float safeInsetY = std::clamp(insetTexels, 0.0f, std::max(0.0f, (static_cast<float>(rectPixels.w) - 1.0f) * 0.5f));

        const float uvMinX = (static_cast<float>(rectPixels.x) + safeInsetX) * invW;
        const float uvMinY = (static_cast<float>(rectPixels.y) + safeInsetY) * invH;
        const float uvMaxX = (static_cast<float>(rectPixels.x + rectPixels.z) - safeInsetX) * invW;
        const float uvMaxY = (static_cast<float>(rectPixels.y + rectPixels.w) - safeInsetY) * invH;

        return glm::vec4(uvMinX, uvMinY, uvMaxX, uvMaxY);
    }

    bool TryParseSubSpriteAssetKey(const std::string& assetKey,
                                   std::string& outTextureAssetKey,
                                   int32_t& outSubSpriteIndex)
    {
        outTextureAssetKey.clear();
        outSubSpriteIndex = -1;

        if (assetKey.empty())
            return false;

        const size_t hashPos = assetKey.rfind('#');
        if (hashPos == std::string::npos || hashPos == 0 || hashPos + 1 >= assetKey.size())
            return false;

        const std::string indexText = assetKey.substr(hashPos + 1);
        if (indexText.empty())
            return false;
        if (!std::all_of(indexText.begin(), indexText.end(), [](unsigned char ch) {
                return ch >= '0' && ch <= '9';
            }))
        {
            return false;
        }

        int64_t parsedIndex = -1;
        try
        {
            parsedIndex = std::stoll(indexText);
        }
        catch (...)
        {
            return false;
        }
        if (parsedIndex < 0 || parsedIndex > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
            return false;

        const std::string textureKey = assetKey.substr(0, hashPos);
        if (textureKey.empty())
            return false;

        outTextureAssetKey = textureKey;
        outSubSpriteIndex = static_cast<int32_t>(parsedIndex);
        return true;
    }
}
