#include "EditorProjectPanel.h"

#include "EditorAssetNaming.h"
#include "EditorPanelStyle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAssetImporter.h"
#include "Assets/TextureAsset.h"
#include "Assets/TilePaletteAsset.h"
#include "EditorTilePalettePanel.h"
#include "Core/Debug/Log.h"
#include "Graphics/NativeRenderHandles.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
        constexpr const char* kAssetMultiSelectionPayload = "ASSET_MULTI_KEYS";
        constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";

        std::vector<std::string> ParseAssetKeyListPayload(const void* payloadData, int payloadSize)
        {
            std::vector<std::string> keys;
            if (!payloadData || payloadSize <= 0)
                return keys;

            std::string payloadText(static_cast<const char*>(payloadData), static_cast<size_t>(payloadSize));
            while (!payloadText.empty() && payloadText.back() == '\0')
                payloadText.pop_back();
            if (payloadText.empty())
                return keys;

            size_t lineStart = 0;
            while (lineStart < payloadText.size())
            {
                const size_t lineEnd = payloadText.find('\n', lineStart);
                const size_t count = (lineEnd == std::string::npos) ? (payloadText.size() - lineStart) : (lineEnd - lineStart);
                std::string key = payloadText.substr(lineStart, count);
                if (!key.empty())
                    keys.push_back(std::move(key));
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
            }

            return keys;
        }

        std::string EncodeAssetKeyListPayload(const std::vector<std::string>& keys)
        {
            std::string payloadText;
            for (const auto& key : keys)
            {
                if (key.empty())
                    continue;
                if (!payloadText.empty())
                    payloadText.push_back('\n');
                payloadText += key;
            }
            return payloadText;
        }
        constexpr uint8_t kScriptPairHeaderBit = 1u << 0u;
        constexpr uint8_t kScriptPairSourceBit = 1u << 1u;
        constexpr std::chrono::milliseconds kDirectoryCacheRefreshInterval(250);

        struct ProjectAssetTreeEntry
        {
            std::filesystem::path AbsolutePath;
            std::filesystem::path RelativePath;
            std::string FileName;
            std::string LowerFileName;
            std::string LowerExtension;
            std::string AssetKey;
            bool IsDirectory = false;
        };

        struct ProjectAssetDirectoryCacheEntry
        {
            std::vector<ProjectAssetTreeEntry> Entries;
            std::chrono::steady_clock::time_point LastRefreshTime = {};
        };

        std::unordered_map<std::string, ProjectAssetDirectoryCacheEntry> gProjectAssetDirectoryCache;

        // Cache for sprite import settings to avoid reading meta files every frame.
        struct SpriteSettingsCacheEntry
        {
            Assets::SpriteImportSettings Settings;
            std::chrono::steady_clock::time_point LoadTime = {};
        };
        struct TextureThumbnailCacheEntry
        {
            Assets::TextureAsset::Ptr TextureAsset;
            std::chrono::steady_clock::time_point LoadTime = {};
        };
        std::unordered_map<std::string, SpriteSettingsCacheEntry> gSpriteSettingsCache;
        std::unordered_map<std::string, TextureThumbnailCacheEntry> gTextureThumbnailCache;
        std::string gProjectSearchFilterLower;
        std::unordered_map<std::string, bool> gProjectSearchMatchCache;
        constexpr std::chrono::milliseconds kSpriteSettingsCacheLifetime(2000);
        constexpr std::chrono::milliseconds kTextureThumbnailCacheLifetime(2000);

        struct AssetTypeBadgeInfo
        {
            const char* Label;
            ImU32 FillColor;
            ImU32 BorderColor;
            ImU32 TextColor;
        };

        constexpr AssetTypeBadgeInfo kBadgeFolder          = { "FLD", IM_COL32( 55, 120, 190, 255), IM_COL32(100, 170, 240, 255), IM_COL32(230, 245, 255, 255) };
        constexpr AssetTypeBadgeInfo kBadgeTexture         = { "TEX", IM_COL32( 45, 145,  70, 255), IM_COL32( 90, 200, 120, 255), IM_COL32(230, 255, 235, 255) };
        constexpr AssetTypeBadgeInfo kBadgeScene           = { "SCN", IM_COL32(185, 120,  30, 255), IM_COL32(235, 175,  65, 255), IM_COL32(255, 245, 225, 255) };
        constexpr AssetTypeBadgeInfo kBadgeMaterial        = { "MAT", IM_COL32(120,  60, 175, 255), IM_COL32(170, 110, 225, 255), IM_COL32(240, 230, 255, 255) };
        constexpr AssetTypeBadgeInfo kBadgeAudio           = { "SND", IM_COL32(170,  50, 120, 255), IM_COL32(220, 100, 170, 255), IM_COL32(255, 230, 245, 255) };
        constexpr AssetTypeBadgeInfo kBadgeFont            = { "FNT", IM_COL32( 40, 140, 150, 255), IM_COL32( 80, 195, 200, 255), IM_COL32(225, 250, 252, 255) };
        constexpr AssetTypeBadgeInfo kBadgePrefab          = { "PFB", IM_COL32( 58, 125, 198, 255), IM_COL32(120, 190, 255, 255), IM_COL32(235, 245, 255, 255) };
        constexpr AssetTypeBadgeInfo kBadgeScript          = { "CPP", IM_COL32(165, 145,  35, 255), IM_COL32(215, 200,  80, 255), IM_COL32(255, 252, 225, 255) };
        constexpr AssetTypeBadgeInfo kBadgeShader          = { "SHD", IM_COL32( 30, 150, 180, 255), IM_COL32( 70, 200, 230, 255), IM_COL32(225, 250, 255, 255) };
        constexpr AssetTypeBadgeInfo kBadgeAudioMixer      = { "MIX", IM_COL32(155,  55, 140, 255), IM_COL32(210, 110, 195, 255), IM_COL32(255, 230, 250, 255) };
        constexpr AssetTypeBadgeInfo kBadgeInputActions    = { "INP", IM_COL32( 70, 155,  50, 255), IM_COL32(120, 210, 100, 255), IM_COL32(235, 255, 230, 255) };
        constexpr AssetTypeBadgeInfo kBadgeAnimationClip   = { "ANI", IM_COL32(190,  65,  55, 255), IM_COL32(240, 115, 105, 255), IM_COL32(255, 232, 230, 255) };
        constexpr AssetTypeBadgeInfo kBadgeAnimController  = { "ACT", IM_COL32(175,  45,  70, 255), IM_COL32(230,  95, 120, 255), IM_COL32(255, 230, 235, 255) };
        constexpr AssetTypeBadgeInfo kBadgeSubSprite       = { "SUB", IM_COL32( 80, 130,  80, 255), IM_COL32(130, 185, 130, 255), IM_COL32(235, 255, 235, 255) };
        constexpr AssetTypeBadgeInfo kBadgeTileset         = { "TLS", IM_COL32(140, 110,  50, 255), IM_COL32(195, 165,  90, 255), IM_COL32(255, 248, 230, 255) };
        constexpr AssetTypeBadgeInfo kBadgeTilePalette     = { "PAL", IM_COL32(110,  95,  55, 255), IM_COL32(180, 155,  95, 255), IM_COL32(255, 246, 225, 255) };
        constexpr AssetTypeBadgeInfo kBadgeUnknown         = { "---", IM_COL32( 90,  90, 100, 255), IM_COL32(140, 140, 155, 255), IM_COL32(220, 220, 230, 255) };

        void DrawAssetTypeBadge(const AssetTypeBadgeInfo& badge, float indentScreenX)
        {
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec2 textSize = ImGui::CalcTextSize(badge.Label);
            const float padX = 5.0f;
            const float padY = 2.0f;
            const float pillW = textSize.x + padX * 2.0f;
            const float pillH = textSize.y + padY * 2.0f;
            const float rowCenterY = itemMin.y + (itemMax.y - itemMin.y) * 0.5f;
            const float labelStartX = indentScreenX + ImGui::GetTreeNodeToLabelSpacing();
            const ImVec2 pillMin(labelStartX, rowCenterY - pillH * 0.5f);
            const ImVec2 pillMax(pillMin.x + pillW, pillMin.y + pillH);
            const ImVec2 textPos(pillMin.x + padX, pillMin.y + padY);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(pillMin, pillMax, badge.FillColor, 4.0f);
            drawList->AddRect(pillMin, pillMax, badge.BorderColor, 4.0f, 0, 1.0f);
            drawList->AddText(textPos, badge.TextColor, badge.Label);
        }

        std::string BadgePadLabel(const std::string& label)
        {
            const float badgeWidth = ImGui::CalcTextSize("XXX").x + 14.0f;
            const float spaceWidth = ImGui::CalcTextSize(" ").x;
            const int numSpaces = static_cast<int>(badgeWidth / spaceWidth) + 2;
            return std::string(static_cast<size_t>(numSpaces), ' ') + label;
        }

        const AssetTypeBadgeInfo& ResolveAssetBadge(
            bool isTexture, bool isScene, bool isMaterial, bool isAudioMixer,
            bool isInputActions, bool isAnimationClip, bool isAnimatorController,
            bool isPrefab, bool isShader, bool isAudio, bool isFont,
            bool isNativeScriptFile)
        {
            if (isTexture)             return kBadgeTexture;
            if (isScene)               return kBadgeScene;
            if (isMaterial)            return kBadgeMaterial;
            if (isAudioMixer)          return kBadgeAudioMixer;
            if (isInputActions)        return kBadgeInputActions;
            if (isAnimationClip)       return kBadgeAnimationClip;
            if (isAnimatorController)  return kBadgeAnimController;
            if (isPrefab)              return kBadgePrefab;
            if (isShader)              return kBadgeShader;
            if (isAudio)               return kBadgeAudio;
            if (isFont)                return kBadgeFont;
            if (isNativeScriptFile)    return kBadgeScript;
            return kBadgeUnknown;
        }

        const Assets::SpriteImportSettings& GetCachedSpriteImportSettings(const std::string& textureAssetKey)
        {
            auto it = gSpriteSettingsCache.find(textureAssetKey);
            const auto now = std::chrono::steady_clock::now();

            if (it != gSpriteSettingsCache.end() &&
                (now - it->second.LoadTime) < kSpriteSettingsCacheLifetime)
            {
                return it->second.Settings;
            }

            SpriteSettingsCacheEntry entry;
            entry.Settings = Assets::LoadSpriteImportSettings(textureAssetKey);
            entry.LoadTime = now;
            auto [insertedIt, _] = gSpriteSettingsCache.insert_or_assign(textureAssetKey, std::move(entry));
            return insertedIt->second.Settings;
        }

        Assets::TextureAsset::Ptr GetCachedThumbnailTextureAsset(const std::string& textureAssetKey)
        {
            if (textureAssetKey.empty())
                return nullptr;

            const auto now = std::chrono::steady_clock::now();
            if (auto it = gTextureThumbnailCache.find(textureAssetKey); it != gTextureThumbnailCache.end())
            {
                if ((now - it->second.LoadTime) < kTextureThumbnailCacheLifetime)
                    return it->second.TextureAsset;
            }

            TextureThumbnailCacheEntry entry;
            entry.TextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(textureAssetKey);
            entry.LoadTime = now;
            auto [insertedIt, _] = gTextureThumbnailCache.insert_or_assign(textureAssetKey, std::move(entry));
            return insertedIt->second.TextureAsset;
        }

        std::string ToLowerAscii(std::string value)
        {
            for (char& character : value)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        bool IsTextureExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".png" || lowerExtension == ".jpg" || lowerExtension == ".jpeg" ||
                   lowerExtension == ".ppm" || lowerExtension == ".pnm" || lowerExtension == ".bmp" ||
                   lowerExtension == ".tga" || lowerExtension == ".gif";
        }

        bool IsSceneFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.size() >= 11 && lowerFileName.rfind(".scene.json") == (lowerFileName.size() - 11);
        }

        bool IsMaterialFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* materialSuffix = ".material.json";
            const std::string suffixString = materialSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsAudioMixerFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* audioMixerSuffix = ".audiomixer.json";
            const std::string suffixString = audioMixerSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsInputActionsFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* inputActionsSuffix = ".inputactions.json";
            const std::string suffixString = inputActionsSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsPrefabFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* prefabSuffix = ".prefab.json";
            const std::string suffixString = prefabSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsAnimationClipFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.ends_with(".animationclip.json") ||
                   lowerFileName.ends_with(".animation.json") ||
                   lowerFileName.ends_with(".anim.json");
        }

        bool IsAnimatorControllerFileNameLower(const std::string& lowerFileName)
        {
            return lowerFileName.ends_with(".animcontroller.json") ||
                   lowerFileName.ends_with(".animatorcontroller.json");
        }

        bool IsTilesetFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* tilesetSuffix = ".tileset.json";
            const std::string suffixString = tilesetSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsTilePaletteFileNameLower(const std::string& lowerFileName)
        {
            constexpr const char* tilePaletteSuffix = ".tilepalette.json";
            const std::string suffixString = tilePaletteSuffix;
            return lowerFileName.size() >= suffixString.size() &&
                   lowerFileName.rfind(suffixString) == (lowerFileName.size() - suffixString.size());
        }

        bool IsShaderExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".glsl";
        }

        bool IsAudioExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".wav" || lowerExtension == ".mp3" || lowerExtension == ".ogg" || lowerExtension == ".flac";
        }

        bool IsFontExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".ttf" || lowerExtension == ".otf";
        }

        bool IsNativeScriptExtensionLower(const std::string& lowerExtension)
        {
            return lowerExtension == ".h" || lowerExtension == ".cpp";
        }

        std::vector<ProjectAssetTreeEntry> ScanProjectDirectoryEntries(const std::filesystem::path& assetsDirectory,
                                                                       const std::filesystem::path& relativePath)
        {
            std::vector<ProjectAssetTreeEntry> entries;
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            std::error_code errorCode;
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory, errorCode))
            {
                if (errorCode)
                    continue;

                ProjectAssetTreeEntry nextEntry;
                nextEntry.AbsolutePath = entry.path();
                nextEntry.FileName = nextEntry.AbsolutePath.filename().string();
                if (nextEntry.FileName.empty() || nextEntry.FileName[0] == '.')
                    continue;
                if (nextEntry.FileName == "Cache")
                    continue;

                std::error_code isDirectoryError;
                nextEntry.IsDirectory = entry.is_directory(isDirectoryError);
                if (isDirectoryError)
                    nextEntry.IsDirectory = false;

                nextEntry.LowerFileName = ToLowerAscii(nextEntry.FileName);
                if (!nextEntry.IsDirectory)
                {
                    nextEntry.LowerExtension = ToLowerAscii(nextEntry.AbsolutePath.extension().string());
                    if (nextEntry.LowerExtension == ".meta")
                        continue;
                }

                nextEntry.RelativePath = relativePath / nextEntry.FileName;
                nextEntry.AssetKey = "Assets/" + nextEntry.RelativePath.generic_string();
                entries.push_back(std::move(nextEntry));
            }

            std::sort(entries.begin(), entries.end(), [](const ProjectAssetTreeEntry& left, const ProjectAssetTreeEntry& right) {
                if (left.IsDirectory != right.IsDirectory)
                    return left.IsDirectory;
                return left.FileName < right.FileName;
            });

            return entries;
        }

        const std::vector<ProjectAssetTreeEntry>& GetCachedProjectDirectoryEntries(const std::filesystem::path& assetsDirectory,
                                                                                    const std::filesystem::path& relativePath)
        {
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            const std::string cacheKey = currentDirectory.lexically_normal().generic_string();
            ProjectAssetDirectoryCacheEntry& cacheEntry = gProjectAssetDirectoryCache[cacheKey];

            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const bool shouldRefresh = cacheEntry.Entries.empty() ||
                (now - cacheEntry.LastRefreshTime) >= kDirectoryCacheRefreshInterval;
            if (shouldRefresh)
            {
                cacheEntry.Entries = ScanProjectDirectoryEntries(assetsDirectory, relativePath);
                cacheEntry.LastRefreshTime = now;
            }

            return cacheEntry.Entries;
        }

        void InvalidateProjectDirectoryCache()
        {
            gProjectAssetDirectoryCache.clear();
            gTextureThumbnailCache.clear();
        }

        void MoveAssetOrFolderToTargetFolder(const char* assetOrFolderKey,
                                             const std::filesystem::path& destinationFolderRelativePath)
        {
            if (!assetOrFolderKey || !assetOrFolderKey[0])
                return;
            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(assetOrFolderKey, textureKey, subSpriteIndex))
                return;

            const bool moved = std::filesystem::path(assetOrFolderKey).extension().empty()
                ? ProjectAssetOperations::MoveFolderToFolder(assetOrFolderKey, destinationFolderRelativePath)
                : ProjectAssetOperations::MoveAssetToFolder(assetOrFolderKey, destinationFolderRelativePath);
            if (moved)
                InvalidateProjectDirectoryCache();
        }

        void MoveAssetListToTargetFolder(const std::vector<std::string>& assetKeys,
                                         const std::filesystem::path& destinationFolderRelativePath)
        {
            bool movedAny = false;
            for (const auto& key : assetKeys)
            {
                if (key.empty())
                    continue;
                std::string textureKey;
                int32_t subSpriteIndex = -1;
                if (Assets::TryParseSubSpriteAssetKey(key, textureKey, subSpriteIndex))
                    continue;
                const bool moved = std::filesystem::path(key).extension().empty()
                    ? ProjectAssetOperations::MoveFolderToFolder(key.c_str(), destinationFolderRelativePath)
                    : ProjectAssetOperations::MoveAssetToFolder(key.c_str(), destinationFolderRelativePath);
                movedAny |= moved;
            }

            if (movedAny)
                InvalidateProjectDirectoryCache();
        }

        void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
        {
            if (!source)
            {
                destination[0] = '\0';
                return;
            }

            std::snprintf(destination.data(), destination.size(), "%s", source);
        }

        std::string BuildScriptAssetDisplayName(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            const std::string stem = path.stem().string();
            if (extension == ".h")
                return stem + " [.h Header]";
            return stem + " [.cpp Source]";
        }

        std::string SanitizeScriptClassBaseName(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
                value.pop_back();

            std::string sanitized;
            sanitized.reserve(value.size() + 8);
            for (char character : value)
            {
                const unsigned char raw = static_cast<unsigned char>(character);
                if (std::isalnum(raw) || character == '_')
                    sanitized.push_back(character);
            }

            if (sanitized.empty())
                sanitized = "NewNativeScript";
            if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0)
                sanitized.insert(0, "Script_");
            return sanitized;
        }

        bool CreateNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                            const std::filesystem::path& parentRelativePath,
                                            const std::string& requestedClassName,
                                            std::string& outCreatedSourceAssetKey,
                                            std::string& outError)
        {
            const std::string className = SanitizeScriptClassBaseName(requestedClassName);
            const std::filesystem::path scriptDirectory = assetsDirectory / parentRelativePath;

            std::error_code errorCode;
            std::filesystem::create_directories(scriptDirectory, errorCode);
            if (errorCode)
            {
                outError = "Failed to create script directory: " + errorCode.message();
                return false;
            }

            const std::filesystem::path headerPath = scriptDirectory / (className + ".h");
            const std::filesystem::path sourcePath = scriptDirectory / (className + ".cpp");
            if (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath))
            {
                outError = "Script already exists: " + className;
                return false;
            }

            const std::string headerTemplate =
                "#pragma once\n\n"
                "#include \"Limitless.h\"\n\n"
                "class " + className + " final : public Limitless::ScriptableEntity\n"
                "{\n"
                "public:\n"
                "    float RotationSpeed = 90.0f;\n\n"
                "    LT_EXPOSED_FIELDS(RotationSpeed)\n\n"
                "protected:\n"
                "    void OnCreate() override;\n"
                "    void OnUpdate(float deltaTime) override;\n"
                "    void OnDestroy() override;\n"
                "};\n";

            const std::string sourceTemplate =
                "#include \"" + className + ".h\"\n\n"
                "#include \"ScriptCoreRegistration.h\"\n\n"
                "void " + className + "::OnCreate()\n"
                "{\n"
                "}\n\n"
                "void " + className + "::OnUpdate(float deltaTime)\n"
                "{\n"
                "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
                "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
                "    if (transform.Rotation.z > 360.0f)\n"
                "        transform.Rotation.z -= 360.0f;\n"
                "}\n\n"
                "void " + className + "::OnDestroy()\n"
                "{\n"
                "}\n\n"
                "LT_REGISTER_SCRIPTCORE_SCRIPT(" + className + ");\n";

            {
                std::ofstream headerOutput(headerPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!headerOutput.is_open())
                {
                    outError = "Failed to create header file: " + headerPath.string();
                    return false;
                }
                headerOutput << headerTemplate;
            }

            {
                std::ofstream sourceOutput(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!sourceOutput.is_open())
                {
                    outError = "Failed to create source file: " + sourcePath.string();
                    return false;
                }
                sourceOutput << sourceTemplate;
            }

            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            outCreatedSourceAssetKey = "Assets/" + (parentRelativePath / (className + ".cpp")).generic_string();
            outError.clear();
            InvalidateProjectDirectoryCache();
            return true;
        }

        std::string EscapeRegexLiteral(const std::string& value)
        {
            std::string escaped;
            escaped.reserve(value.size() * 2);
            for (char character : value)
            {
                switch (character)
                {
                    case '.': case '^': case '$': case '|': case '(': case ')':
                    case '[': case ']': case '{': case '}': case '*': case '+':
                    case '?': case '\\':
                        escaped.push_back('\\');
                        break;
                    default:
                        break;
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        bool ReplaceWholeWordInFile(const std::filesystem::path& filePath,
                                    const std::string& oldWord,
                                    const std::string& newWord)
        {
            std::ifstream input(filePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::regex wholeWordPattern("\\b" + EscapeRegexLiteral(oldWord) + "\\b");
            content = std::regex_replace(content, wholeWordPattern, newWord);

            std::ofstream output(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }

        bool RewriteSourceIncludeForRenamedScriptPair(const std::filesystem::path& sourcePath,
                                                      const std::string& oldHeaderName,
                                                      const std::string& newHeaderName)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::string oldInclude = "#include \"" + oldHeaderName + "\"";
            const std::string newInclude = "#include \"" + newHeaderName + "\"";
            const size_t includePosition = content.find(oldInclude);
            if (includePosition == std::string::npos)
                return true;
            content.replace(includePosition, oldInclude.size(), newInclude);

            std::ofstream output(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }

        bool RenameNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                            const std::filesystem::path& scriptRelativePath,
                                            const std::string& newDisplayName,
                                            std::filesystem::path& outNewHeaderRelativePath,
                                            std::filesystem::path& outNewSourceRelativePath)
        {
            const std::string sanitizedBaseName = SanitizeScriptClassBaseName(newDisplayName);

            const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
            const std::string oldClassName = baseRelativePath.stem().string();
            const std::string newClassName = sanitizedBaseName;
            const std::filesystem::path headerRelativePath = baseRelativePath.string() + ".h";
            const std::filesystem::path sourceRelativePath = baseRelativePath.string() + ".cpp";
            const std::filesystem::path headerPath = assetsDirectory / headerRelativePath;
            const std::filesystem::path sourcePath = assetsDirectory / sourceRelativePath;

            std::error_code errorCode;
            if (!std::filesystem::exists(headerPath, errorCode) || !std::filesystem::exists(sourcePath, errorCode))
                return false;

            const std::filesystem::path newBaseRelativePath = baseRelativePath.parent_path() / sanitizedBaseName;
            outNewHeaderRelativePath = newBaseRelativePath.string() + ".h";
            outNewSourceRelativePath = newBaseRelativePath.string() + ".cpp";
            const std::filesystem::path newHeaderPath = assetsDirectory / outNewHeaderRelativePath;
            const std::filesystem::path newSourcePath = assetsDirectory / outNewSourceRelativePath;

            if (newHeaderPath == headerPath && newSourcePath == sourcePath)
                return true;
            if (std::filesystem::exists(newHeaderPath, errorCode) || std::filesystem::exists(newSourcePath, errorCode))
                return false;

            std::filesystem::rename(headerPath, newHeaderPath, errorCode);
            if (errorCode)
                return false;
            std::filesystem::rename(sourcePath, newSourcePath, errorCode);
            if (errorCode)
                return false;

            const std::filesystem::path oldHeaderMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
            const std::filesystem::path oldSourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
            const std::filesystem::path newHeaderMetaPath = newHeaderPath.parent_path() / (newHeaderPath.filename().string() + ".meta");
            const std::filesystem::path newSourceMetaPath = newSourcePath.parent_path() / (newSourcePath.filename().string() + ".meta");
            if (std::filesystem::exists(oldHeaderMetaPath, errorCode))
                std::filesystem::rename(oldHeaderMetaPath, newHeaderMetaPath, errorCode);
            errorCode.clear();
            if (std::filesystem::exists(oldSourceMetaPath, errorCode))
                std::filesystem::rename(oldSourceMetaPath, newSourceMetaPath, errorCode);

            (void)RewriteSourceIncludeForRenamedScriptPair(newSourcePath, headerPath.filename().string(), newHeaderPath.filename().string());
            (void)ReplaceWholeWordInFile(newHeaderPath, oldClassName, newClassName);
            (void)ReplaceWholeWordInFile(newSourcePath, oldClassName, newClassName);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            InvalidateProjectDirectoryCache();
            return true;
        }

        bool DeleteNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory, const std::filesystem::path& scriptRelativePath)
        {
            const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
            const std::filesystem::path headerPath = assetsDirectory / (baseRelativePath.string() + ".h");
            const std::filesystem::path sourcePath = assetsDirectory / (baseRelativePath.string() + ".cpp");

            std::error_code errorCode;
            bool removedAny = false;
            if (std::filesystem::exists(headerPath, errorCode))
                removedAny |= std::filesystem::remove(headerPath, errorCode);
            errorCode.clear();
            if (std::filesystem::exists(sourcePath, errorCode))
                removedAny |= std::filesystem::remove(sourcePath, errorCode);
            errorCode.clear();

            const std::filesystem::path headerMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
            const std::filesystem::path sourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
            std::filesystem::remove(headerMetaPath, errorCode);
            errorCode.clear();
            std::filesystem::remove(sourceMetaPath, errorCode);

            if (removedAny)
            {
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
                InvalidateProjectDirectoryCache();
            }
            return removedAny;
        }

        bool DeleteAssetKeysInAssets(const std::filesystem::path& assetsDirectory, const std::vector<std::string>& assetKeys);

        bool IsSceneAssetKey(const std::string& assetKey)
        {
            if (assetKey.rfind("Assets/", 0) != 0)
                return false;
            const std::string lowerKey = ToLowerAscii(assetKey);
            return lowerKey.ends_with(".scene.json");
        }

        bool DeleteAssetKeysWithSceneHandling(
            const std::filesystem::path& assetsDirectory,
            const std::vector<std::string>& assetKeys,
            const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested)
        {
            if (assetKeys.empty())
                return false;

            std::vector<std::string> deduplicatedSceneKeys;
            std::vector<std::string> deduplicatedNonSceneKeys;
            std::unordered_set<std::string> deduplicatedKeys;
            deduplicatedKeys.reserve(assetKeys.size());

            for (const std::string& assetKey : assetKeys)
            {
                if (assetKey.empty() || !deduplicatedKeys.insert(assetKey).second)
                    continue;

                std::string textureKey;
                int32_t subSpriteIndex = -1;
                if (Assets::TryParseSubSpriteAssetKey(assetKey, textureKey, subSpriteIndex))
                    continue;

                if (IsSceneAssetKey(assetKey))
                    deduplicatedSceneKeys.push_back(assetKey);
                else
                    deduplicatedNonSceneKeys.push_back(assetKey);
            }

            bool removedAny = false;
            if (!deduplicatedSceneKeys.empty())
            {
                if (onDeleteSceneAssetsRequested)
                    removedAny |= onDeleteSceneAssetsRequested(deduplicatedSceneKeys);
                else
                    removedAny |= DeleteAssetKeysInAssets(assetsDirectory, deduplicatedSceneKeys);
            }

            if (!deduplicatedNonSceneKeys.empty())
                removedAny |= DeleteAssetKeysInAssets(assetsDirectory, deduplicatedNonSceneKeys);

            if (removedAny)
                InvalidateProjectDirectoryCache();

            return removedAny;
        }

        bool DeleteAssetKeysInAssets(const std::filesystem::path& assetsDirectory, const std::vector<std::string>& assetKeys)
        {
            bool removedAny = false;
            bool removedRegularAsset = false;
            std::unordered_set<std::string> deduplicatedKeys;
            deduplicatedKeys.reserve(assetKeys.size());

            for (const std::string& assetKey : assetKeys)
            {
                if (assetKey.empty() || !deduplicatedKeys.insert(assetKey).second)
                    continue;

                std::string textureKey;
                int32_t subSpriteIndex = -1;
                if (Assets::TryParseSubSpriteAssetKey(assetKey, textureKey, subSpriteIndex))
                    continue;

                if (assetKey.rfind("Assets/", 0) != 0)
                    continue;

                const std::filesystem::path relativePath = std::filesystem::path(assetKey.substr(7));
                const std::filesystem::path absolutePath = assetsDirectory / relativePath;
                const std::string lowerExtension = ToLowerAscii(absolutePath.extension().string());

                if (IsNativeScriptExtensionLower(lowerExtension))
                {
                    removedAny |= DeleteNativeScriptPairInAssets(assetsDirectory, relativePath);
                    continue;
                }

                std::error_code deleteErrorCode;
                const bool removed = std::filesystem::remove(absolutePath, deleteErrorCode);
                if (!removed || deleteErrorCode)
                    continue;

                removedAny = true;
                removedRegularAsset = true;
                const std::filesystem::path metaPath = absolutePath.parent_path() / (absolutePath.filename().string() + ".meta");
                std::filesystem::remove(metaPath, deleteErrorCode);
            }

            if (removedRegularAsset)
            {
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
                InvalidateProjectDirectoryCache();
            }

            return removedAny;
        }

        std::string GetAssetDisplayName(const std::filesystem::path& path)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromPath(path);
        }

        bool MatchesProjectSearchFilter(const std::string& value)
        {
            if (gProjectSearchFilterLower.empty())
                return true;
            return ToLowerAscii(value).find(gProjectSearchFilterLower) != std::string::npos;
        }

        bool EntryMatchesProjectSearchFilter(const ProjectAssetTreeEntry& entry, const std::string& displayName)
        {
            if (gProjectSearchFilterLower.empty())
                return true;

            return MatchesProjectSearchFilter(displayName) ||
                   MatchesProjectSearchFilter(entry.FileName) ||
                   MatchesProjectSearchFilter(entry.AssetKey);
        }

        bool DirectoryContainsProjectSearchMatch(const std::filesystem::path& assetsDirectory, const std::filesystem::path& relativePath)
        {
            if (gProjectSearchFilterLower.empty())
                return true;

            const std::string cacheKey = relativePath.generic_string();
            if (const auto cachedIt = gProjectSearchMatchCache.find(cacheKey); cachedIt != gProjectSearchMatchCache.end())
                return cachedIt->second;

            const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(assetsDirectory, relativePath);
            for (const ProjectAssetTreeEntry& entry : entries)
            {
                if (entry.IsDirectory)
                {
                    if (MatchesProjectSearchFilter(entry.FileName) || DirectoryContainsProjectSearchMatch(assetsDirectory, entry.RelativePath))
                    {
                        gProjectSearchMatchCache[cacheKey] = true;
                        return true;
                    }
                    continue;
                }

                const std::string displayName = IsNativeScriptExtensionLower(entry.LowerExtension)
                    ? BuildScriptAssetDisplayName(entry.AbsolutePath)
                    : GetAssetDisplayName(entry.AbsolutePath);
                if (EntryMatchesProjectSearchFilter(entry, displayName))
                {
                    gProjectSearchMatchCache[cacheKey] = true;
                    return true;
                }
            }

            gProjectSearchMatchCache[cacheKey] = false;
            return false;
        }

        void DrawAssetTree(const std::filesystem::path& assetsDirectory,
                           const std::filesystem::path& relativePath,
                           EditorProjectPanelState& state,
                           entt::entity& selectedEntity,
                           std::string& selectedTextureAssetKey,
                           Assets::TextureAsset::Ptr& cachedTextureAsset,
                           std::string& selectedMaterialAssetKey,
                           Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                           std::string& selectedNativeScriptAssetKey,
                           std::string& selectedPrefabAssetKey,
                           std::string& selectedTilesetAssetKey,
                           std::string& selectedAudioMixerAssetKey,
                           std::string& selectedInputActionsAssetKey,
                          std::string& selectedAnimationClipAssetKey,
                          std::string& selectedAnimatorControllerAssetKey,
                           const char* texturePayloadId,
                           const char* audioPayloadId,
                           const char* assetMovePayloadId,
                           const char* scenePayloadId,
                           const char* materialPayloadId,
                           const char* prefabPayloadId,
                           const char* shaderPayloadId,
                           const char* fontPayloadId,
                           const std::function<void(const std::string&)>& onSceneActivated,
                           const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                           const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
                          const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                          const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                           const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
                           const std::function<void(const std::string&)>& onPrefabOpened,
                           const std::function<void(const std::string&)>& onPrefabInstantiated,
                           const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
                           const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
                           const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested,
                           const std::function<void(const std::string&)>& onNativeScriptAssetActivated)
        {
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            std::error_code errorCode;
            if (!std::filesystem::exists(currentDirectory, errorCode) || !std::filesystem::is_directory(currentDirectory, errorCode))
                return;

            const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(assetsDirectory, relativePath);
            std::vector<std::string> directoryAssetKeys;
            directoryAssetKeys.reserve(entries.size());
            for (const auto& entry : entries)
            {
                if (!entry.IsDirectory && !entry.AssetKey.empty())
                    directoryAssetKeys.push_back(entry.AssetKey);
            }

            std::unordered_map<std::string, uint8_t> scriptPairPresenceByBasePath;
            scriptPairPresenceByBasePath.reserve(entries.size());
            for (const ProjectAssetTreeEntry& entry : entries)
            {
                if (entry.IsDirectory || !IsNativeScriptExtensionLower(entry.LowerExtension))
                    continue;

                const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / std::filesystem::path(entry.FileName).stem();
                const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                uint8_t& presenceBits = scriptPairPresenceByBasePath[scriptBaseKey];
                if (entry.LowerExtension == ".h")
                    presenceBits |= kScriptPairHeaderBit;
                else if (entry.LowerExtension == ".cpp")
                    presenceBits |= kScriptPairSourceBit;
            }

            std::unordered_set<std::string> renderedScriptBasePaths;
            for (const ProjectAssetTreeEntry& entry : entries)
            {
                const std::string& fileName = entry.FileName;
                const bool isDirectory = entry.IsDirectory;
                const std::string& assetKey = entry.AssetKey;
                const std::filesystem::path& entryRelativePath = entry.RelativePath;
                const bool searchActive = !gProjectSearchFilterLower.empty();

                if (isDirectory)
                {
                    if (searchActive && !MatchesProjectSearchFilter(fileName) && !DirectoryContainsProjectSearchMatch(assetsDirectory, entryRelativePath))
                        continue;

                    const std::string folderStateKey = entryRelativePath.generic_string();
                    bool folderExpanded = true;
                    if (!searchActive)
                    {
                        if (const auto expandedStateIt = state.ExpandedFolderState.find(folderStateKey);
                            expandedStateIt != state.ExpandedFolderState.end())
                        {
                            folderExpanded = expandedStateIt->second;
                        }
                    }
                    else
                    {
                        folderExpanded = true;
                    }
                    const bool previousFolderExpanded = folderExpanded;
                    ImGui::SetNextItemOpen(folderExpanded, ImGuiCond_Always);
                    const float folderIndentX = ImGui::GetCursorScreenPos().x;
                    const bool nodeOpen = ImGui::TreeNodeEx(BadgePadLabel(fileName).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding);
                    DrawAssetTypeBadge(kBadgeFolder, folderIndentX);
                    if (!searchActive)
                    {
                        state.ExpandedFolderState[folderStateKey] = nodeOpen;
                        if (nodeOpen != previousFolderExpanded)
                            state.TreeExpansionStateChanged = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                    {
                        state.HoveredFolderRelativePathForExternalDrop = entryRelativePath;
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        state.FolderPopupParent = entryRelativePath;
                        if (ImGui::MenuItem("Create Folder"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Create;
                            CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
                        }
                        if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                            onCreateSceneRequested(entryRelativePath);
                        if (ImGui::MenuItem("Create Material"))
                        {
                            state.CreateMaterialParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                            state.CreateMaterialPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Tile Palette"))
                        {
                            state.CreateTilePaletteParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateTilePaletteNameBuffer, "New Tile Palette");
                            state.CreateTilePalettePopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Audio Mixer"))
                        {
                            state.CreateAudioMixerParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                            state.CreateAudioMixerPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Input Actions"))
                        {
                            state.CreateInputActionsParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateInputActionsNameBuffer, "New Input Actions");
                            state.CreateInputActionsPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Animation Clip"))
                        {
                            state.CreateAnimationClipParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                            state.CreateAnimationClipPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Animator Controller"))
                        {
                            state.CreateAnimatorControllerParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                            state.CreateAnimatorControllerPopupPending = true;
                        }
                        if (ImGui::MenuItem("Create Native Script"))
                        {
                            state.CreateNativeScriptParentRelativePath = entryRelativePath;
                            CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                            state.CreateNativeScriptPopupPending = true;
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Rename;
                            CopyTextToBuffer(state.FolderPopupBuffer, fileName.c_str());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete"))
                        {
                            if (ProjectAssetOperations::DeleteFolderInAssets(assetsDirectory, entryRelativePath))
                            {
                                InvalidateProjectDirectoryCache();
                                LT_INFO("Deleted folder {}", entryRelativePath.generic_string());
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMultiSelectionPayload))
                        {
                            const std::vector<std::string> keys = ParseAssetKeyListPayload(payload->Data, payload->DataSize);
                            MoveAssetListToTargetFolder(keys, entryRelativePath);
                        }
                        else
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            MoveAssetOrFolderToTargetFolder(key, entryRelativePath);
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                        {
                            const auto* entity = static_cast<const entt::entity*>(payload->Data);
                            if (entity && onCreatePrefabFromSceneEntityRequested)
                                onCreatePrefabFromSceneEntityRequested(*entity, entryRelativePath);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        ImGui::Text("%s", fileName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (nodeOpen)
                    {
                        DrawAssetTree(assetsDirectory,
                                      entryRelativePath,
                                      state,
                                      selectedEntity,
                                      selectedTextureAssetKey,
                                      cachedTextureAsset,
                                      selectedMaterialAssetKey,
                                      cachedMaterialAsset,
                                      selectedNativeScriptAssetKey,
                                      selectedPrefabAssetKey,
                                      selectedTilesetAssetKey,
                                      selectedAudioMixerAssetKey,
                                      selectedInputActionsAssetKey,
                                      selectedAnimationClipAssetKey,
                                      selectedAnimatorControllerAssetKey,
                                      texturePayloadId,
                                      audioPayloadId,
                                      assetMovePayloadId,
                                      scenePayloadId,
                                      materialPayloadId,
                                      prefabPayloadId,
                                      shaderPayloadId,
                                      fontPayloadId,
                                      onSceneActivated,
                                      onCreateSceneRequested,
                                      onCreateMaterialRequested,
                                      onCreateTilesetRequested,
                                      onCreateAudioMixerRequested,
                                     onCreateInputActionsRequested,
                                      onCreateAnimationClipRequested,
                                      onCreateAnimatorControllerRequested,
                                      onCreatePrefabFromSceneEntityRequested,
                                      onPrefabOpened,
                                      onPrefabInstantiated,
                                      onSetDefaultSceneRequested,
                                      onAssetRenamed,
                                      onDeleteSceneAssetsRequested,
                                      onNativeScriptAssetActivated);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    if (IsNativeScriptExtensionLower(entry.LowerExtension))
                    {
                        const std::filesystem::path scriptBaseRelativePath = entryRelativePath.parent_path() / entryRelativePath.stem();
                        const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                        if (renderedScriptBasePaths.find(scriptBaseKey) != renderedScriptBasePaths.end())
                            continue;

                        const std::filesystem::path headerRelativePath = scriptBaseRelativePath.string() + ".h";
                        const std::filesystem::path sourceRelativePath = scriptBaseRelativePath.string() + ".cpp";
                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseKey);
                        const bool hasScriptPair =
                            scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                            (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                (kScriptPairHeaderBit | kScriptPairSourceBit);
                        if (hasScriptPair)
                        {
                            renderedScriptBasePaths.insert(scriptBaseKey);
                            const std::string scriptBaseName = scriptBaseRelativePath.stem().string();
                            if (searchActive && !MatchesProjectSearchFilter(scriptBaseName))
                                continue;

                            const std::string scriptNodeLabel = BadgePadLabel(scriptBaseName + " [Native Script]") + "###ScriptPair_" + scriptBaseKey;
                            const std::string sourceAssetKey = "Assets/" + sourceRelativePath.generic_string();
                            const std::string headerAssetKey = "Assets/" + headerRelativePath.generic_string();
                            const bool scriptPairSelected =
                                (selectedNativeScriptAssetKey == sourceAssetKey) ||
                                (selectedNativeScriptAssetKey == headerAssetKey);
                            const ImGuiTreeNodeFlags scriptPairFlags = scriptPairSelected
                                ? (ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding)
                                : (ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding);
                            const float scriptPairIndentX = ImGui::GetCursorScreenPos().x;
                            const bool scriptNodeOpen = ImGui::TreeNodeEx(scriptNodeLabel.c_str(), scriptPairFlags);
                            DrawAssetTypeBadge(kBadgeScript, scriptPairIndentX);

                            const bool selectedScriptPairWithoutDrag =
                                ImGui::IsItemHovered() &&
                                ImGui::IsMouseReleased(0) &&
                                (ImGui::GetDragDropPayload() == nullptr);
                            if (selectedScriptPairWithoutDrag)
                            {
                                state.MultiSelectedAssetKeys.clear();
                                state.MultiSelectedAssetKeys.push_back(sourceAssetKey);
                                state.SelectionAnchorAssetKey = sourceAssetKey;
                                selectedNativeScriptAssetKey = sourceAssetKey;
                                selectedPrefabAssetKey.clear();
                                selectedTextureAssetKey.clear();
                                selectedMaterialAssetKey.clear();
                                selectedTilesetAssetKey.clear();
                                selectedAudioMixerAssetKey.clear();
                                selectedInputActionsAssetKey.clear();
                                selectedEntity = entt::null;
                                cachedTextureAsset.reset();
                                cachedMaterialAsset.reset();
                            }

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                            {
                                onNativeScriptAssetActivated(sourceAssetKey);
                            }

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                            {
                                ImGui::SetDragDropPayload(assetMovePayloadId, sourceAssetKey.c_str(), static_cast<uint32_t>(sourceAssetKey.size() + 1), ImGuiCond_Once);
                                ImGui::Text("%s", scriptBaseName.c_str());
                                ImGui::EndDragDropSource();
                            }

                            if (ImGui::BeginPopupContextItem())
                            {
                                if (ImGui::MenuItem("Open Script") && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(sourceAssetKey);
                                if (ImGui::MenuItem("Rename Script Pair"))
                                {
                                    state.RenameAssetRelativePath = sourceRelativePath;
                                    CopyTextToBuffer(state.RenameAssetBuffer, scriptBaseName.c_str());
                                    state.RenameAssetAsNativeScriptPair = true;
                                    state.RenameAssetPopupPending = true;
                                }
                                if (ImGui::MenuItem("Delete Script Pair"))
                                {
                                    const bool deleteMultiSelection =
                                        state.MultiSelectedAssetKeys.size() > 1 &&
                                        std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), sourceAssetKey) != state.MultiSelectedAssetKeys.end();
                                    const bool removed = deleteMultiSelection
                                        ? DeleteAssetKeysWithSceneHandling(assetsDirectory, state.MultiSelectedAssetKeys, onDeleteSceneAssetsRequested)
                                        : DeleteAssetKeysWithSceneHandling(assetsDirectory, { sourceAssetKey }, onDeleteSceneAssetsRequested);
                                    if (removed)
                                    {
                                        state.MultiSelectedAssetKeys.clear();
                                        state.SelectionAnchorAssetKey.clear();
                                        state.MultiSelectedSubSpriteKeys.clear();
                                        state.SubSpriteSelectionAnchorKey.clear();
                                        selectedNativeScriptAssetKey.clear();
                                        selectedPrefabAssetKey.clear();
                                        selectedTextureAssetKey.clear();
                                        selectedMaterialAssetKey.clear();
                                        selectedTilesetAssetKey.clear();
                                        selectedAudioMixerAssetKey.clear();
                                        selectedInputActionsAssetKey.clear();
                                        selectedEntity = entt::null;
                                        cachedTextureAsset.reset();
                                        cachedMaterialAsset.reset();
                                        InvalidateProjectDirectoryCache();
                                        LT_INFO("Deleted native script pair {}", scriptBaseName);
                                    }
                                }
                                ImGui::EndPopup();
                            }

                            if (scriptNodeOpen)
                            {
                                const std::string headerItemLabel = BadgePadLabel(scriptBaseName + " [.h Header]") + "###ScriptPairHeader_" + scriptBaseKey;
                                const std::string sourceItemLabel = BadgePadLabel(scriptBaseName + " [.cpp Source]") + "###ScriptPairSource_" + scriptBaseKey;
                                const ImGuiTreeNodeFlags headerItemFlags =
                                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                    ((selectedNativeScriptAssetKey == headerAssetKey) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
                                const ImGuiTreeNodeFlags sourceItemFlags =
                                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                    ((selectedNativeScriptAssetKey == sourceAssetKey) ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);

                                const float headerIndentX = ImGui::GetCursorScreenPos().x;
                                ImGui::TreeNodeEx(headerItemLabel.c_str(), headerItemFlags);
                                DrawAssetTypeBadge(kBadgeScript, headerIndentX);
                                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr))
                                {
                                    state.MultiSelectedAssetKeys.clear();
                                    state.MultiSelectedAssetKeys.push_back(headerAssetKey);
                                    state.SelectionAnchorAssetKey = headerAssetKey;
                                    selectedNativeScriptAssetKey = headerAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                    selectedEntity = entt::null;
                                    cachedTextureAsset.reset();
                                    cachedMaterialAsset.reset();
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(headerAssetKey);

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                                {
                                    ImGui::SetDragDropPayload(assetMovePayloadId, headerAssetKey.c_str(), static_cast<uint32_t>(headerAssetKey.size() + 1), ImGuiCond_Once);
                                    ImGui::Text("%s", scriptBaseName.c_str());
                                    ImGui::EndDragDropSource();
                                }

                                const float sourceIndentX = ImGui::GetCursorScreenPos().x;
                                ImGui::TreeNodeEx(sourceItemLabel.c_str(), sourceItemFlags);
                                DrawAssetTypeBadge(kBadgeScript, sourceIndentX);
                                if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr))
                                {
                                    state.MultiSelectedAssetKeys.clear();
                                    state.MultiSelectedAssetKeys.push_back(sourceAssetKey);
                                    state.SelectionAnchorAssetKey = sourceAssetKey;
                                    selectedNativeScriptAssetKey = sourceAssetKey;
                                    selectedPrefabAssetKey.clear();
                                    selectedTextureAssetKey.clear();
                                    selectedMaterialAssetKey.clear();
                                    selectedTilesetAssetKey.clear();
                                    selectedAudioMixerAssetKey.clear();
                                    selectedInputActionsAssetKey.clear();
                                    selectedEntity = entt::null;
                                    cachedTextureAsset.reset();
                                    cachedMaterialAsset.reset();
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(sourceAssetKey);

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                                {
                                    ImGui::SetDragDropPayload(assetMovePayloadId, sourceAssetKey.c_str(), static_cast<uint32_t>(sourceAssetKey.size() + 1), ImGuiCond_Once);
                                    ImGui::Text("%s", scriptBaseName.c_str());
                                    ImGui::EndDragDropSource();
                                }
                                ImGui::TreePop();
                            }
                            continue;
                        }
                    }

                    const bool isTexture = IsTextureExtensionLower(entry.LowerExtension);
                    const bool isScene = IsSceneFileNameLower(entry.LowerFileName);
                    const bool isMaterial = IsMaterialFileNameLower(entry.LowerFileName);
                    const bool isAudioMixer = IsAudioMixerFileNameLower(entry.LowerFileName);
                    const bool isInputActions = IsInputActionsFileNameLower(entry.LowerFileName);
                    const bool isAnimationClip = IsAnimationClipFileNameLower(entry.LowerFileName);
                    const bool isAnimatorController = IsAnimatorControllerFileNameLower(entry.LowerFileName);
                    const bool isPrefab = IsPrefabFileNameLower(entry.LowerFileName);
                    const bool isShader = IsShaderExtensionLower(entry.LowerExtension);
                    const bool isAudio = IsAudioExtensionLower(entry.LowerExtension);
                    const bool isFont = IsFontExtensionLower(entry.LowerExtension);
                    const bool isNativeScriptFile = IsNativeScriptExtensionLower(entry.LowerExtension);
                    const bool hasPairedScriptFile = isNativeScriptFile
                        && ([&]() {
                               const std::filesystem::path scriptBaseRelativePath = entryRelativePath.parent_path() / entryRelativePath.stem();
                               const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseRelativePath.generic_string());
                               return scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                                      (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                          (kScriptPairHeaderBit | kScriptPairSourceBit);
                           })();
                    const std::string displayName = isNativeScriptFile
                        ? BuildScriptAssetDisplayName(entry.AbsolutePath)
                        : GetAssetDisplayName(entry.AbsolutePath);
                    if (searchActive && !EntryMatchesProjectSearchFilter(entry, displayName))
                        continue;

                    const std::string treeLabel = BadgePadLabel(displayName) + "###" + fileName;

                    // Check if this texture has sub-sprites so we can render it as an expandable parent.
                    const bool hasSubSprites = isTexture && [&]() {
                        const auto& spriteSets = GetCachedSpriteImportSettings(assetKey);
                        return spriteSets.Mode == Assets::SpriteImportSettings::SpriteMode::Multiple &&
                               !spriteSets.SubSprites.empty();
                    }();

                    const ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                    const ImGuiTreeNodeFlags expandableFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
                    const ImGuiTreeNodeFlags flags = hasSubSprites ? expandableFlags : leafFlags;
                    std::string selectedTextureParentKey;
                    int32_t selectedTextureSubIndex = -1;
                    const bool selectedTextureIsSubSprite =
                        Assets::TryParseSubSpriteAssetKey(selectedTextureAssetKey, selectedTextureParentKey, selectedTextureSubIndex);
                    (void)selectedTextureSubIndex;
                    const bool isPrimarySelected =
                        (isTexture && (selectedTextureAssetKey == assetKey ||
                                       (selectedTextureIsSubSprite && selectedTextureParentKey == assetKey))) ||
                        (isMaterial && (selectedMaterialAssetKey == assetKey)) ||
                        (isAudioMixer && (selectedAudioMixerAssetKey == assetKey)) ||
                        (isInputActions && (selectedInputActionsAssetKey == assetKey)) ||
                        (isAnimationClip && (selectedAnimationClipAssetKey == assetKey)) ||
                        (isAnimatorController && (selectedAnimatorControllerAssetKey == assetKey)) ||
                        (isNativeScriptFile && (selectedNativeScriptAssetKey == assetKey)) ||
                        (isPrefab && (selectedPrefabAssetKey == assetKey));
                    const bool isMultiSelected = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) != state.MultiSelectedAssetKeys.end();
                    const float assetIndentX = ImGui::GetCursorScreenPos().x;
                    const bool treeNodeOpen = ImGui::TreeNodeEx(treeLabel.c_str(), (isPrimarySelected || isMultiSelected) ? (flags | ImGuiTreeNodeFlags_Selected) : flags);
                    DrawAssetTypeBadge(ResolveAssetBadge(isTexture, isScene, isMaterial, isAudioMixer, isInputActions, isAnimationClip, isAnimatorController, isPrefab, isShader, isAudio, isFont, isNativeScriptFile), assetIndentX);

                    const bool releasedOnItemWithoutDrag =
                        ImGui::IsItemHovered() &&
                        ImGui::IsMouseReleased(0) &&
                        (ImGui::GetDragDropPayload() == nullptr);
                    const auto clearAssetSelection = [&]() {
                        selectedTextureAssetKey.clear();
                        selectedMaterialAssetKey.clear();
                        selectedNativeScriptAssetKey.clear();
                        selectedPrefabAssetKey.clear();
                        selectedTilesetAssetKey.clear();
                        selectedAudioMixerAssetKey.clear();
                        selectedInputActionsAssetKey.clear();
                        selectedEntity = entt::null;
                        cachedTextureAsset.reset();
                        cachedMaterialAsset.reset();
                    };
                    const auto setPrimarySelectionForClickedAsset = [&]() {
                        clearAssetSelection();
                        if (isTexture)
                            selectedTextureAssetKey = assetKey;
                        else if (isMaterial)
                            selectedMaterialAssetKey = assetKey;
                        else if (isNativeScriptFile)
                            selectedNativeScriptAssetKey = assetKey;
                        else if (isPrefab)
                            selectedPrefabAssetKey = assetKey;
                        else if (isAudioMixer)
                            selectedAudioMixerAssetKey = assetKey;
                        else if (isInputActions)
                            selectedInputActionsAssetKey = assetKey;
                    };
                    if (releasedOnItemWithoutDrag)
                    {
                        const ImGuiIO& io = ImGui::GetIO();
                        const bool shiftPressed = io.KeyShift;
                        const bool controlPressed = io.KeyCtrl;
                        state.MultiSelectedSubSpriteKeys.clear();
                        state.SubSpriteSelectionAnchorKey.clear();

                        if (shiftPressed)
                        {
                            auto findIndex = [&](const std::string& key) -> int32_t {
                                for (size_t index = 0; index < directoryAssetKeys.size(); ++index)
                                {
                                    if (directoryAssetKeys[index] == key)
                                        return static_cast<int32_t>(index);
                                }
                                return -1;
                            };

                            const int32_t anchorIndex = findIndex(state.SelectionAnchorAssetKey);
                            const int32_t clickedIndex = findIndex(assetKey);
                            if (!controlPressed)
                                state.MultiSelectedAssetKeys.clear();

                            if (anchorIndex >= 0 && clickedIndex >= 0)
                            {
                                const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                                const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                                for (int32_t index = minIndex; index <= maxIndex; ++index)
                                {
                                    const std::string& rangeKey = directoryAssetKeys[static_cast<size_t>(index)];
                                    if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), rangeKey) == state.MultiSelectedAssetKeys.end())
                                        state.MultiSelectedAssetKeys.push_back(rangeKey);
                                }
                            }
                            else if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) == state.MultiSelectedAssetKeys.end())
                            {
                                state.MultiSelectedAssetKeys.push_back(assetKey);
                            }

                            setPrimarySelectionForClickedAsset();
                        }
                        else if (controlPressed)
                        {
                            const auto foundIt = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey);
                            const bool wasSelected = foundIt != state.MultiSelectedAssetKeys.end();
                            if (wasSelected)
                            {
                                state.MultiSelectedAssetKeys.erase(foundIt);
                                if (state.MultiSelectedAssetKeys.empty())
                                    clearAssetSelection();
                            }
                            else
                            {
                                state.MultiSelectedAssetKeys.push_back(assetKey);
                                setPrimarySelectionForClickedAsset();
                            }
                            state.SelectionAnchorAssetKey = assetKey;
                        }
                        else
                        {
                            state.MultiSelectedAssetKeys.clear();
                            state.MultiSelectedAssetKeys.push_back(assetKey);
                            state.SelectionAnchorAssetKey = assetKey;
                            setPrimarySelectionForClickedAsset();
                        }
                    }

                    if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedTextureAssetKey = assetKey;
                    }
                    else if (isScene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onSceneActivated)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedNativeScriptAssetKey.clear();
                        onSceneActivated(assetKey);
                    }
                    else if (isPrefab && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onPrefabOpened)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedPrefabAssetKey = assetKey;
                        onPrefabOpened(assetKey);
                    }
                    else if (isMaterial && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedMaterialAssetKey = assetKey;
                    }
                    else if (isAudioMixer && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAudioMixerAssetKey = assetKey;
                    }
                    else if (isAnimationClip && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAnimationClipAssetKey = assetKey;
                    }
                    else if (isAnimatorController && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedAnimatorControllerAssetKey = assetKey;
                    }
                    else if (isNativeScriptFile && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                    {
                        state.MultiSelectedAssetKeys.clear();
                        state.MultiSelectedAssetKeys.push_back(assetKey);
                        state.SelectionAnchorAssetKey = assetKey;
                        clearAssetSelection();
                        selectedNativeScriptAssetKey = assetKey;
                        onNativeScriptAssetActivated(assetKey);
                    }

                    if (ImGui::BeginPopupContextItem())
                    {
                        if (isNativeScriptFile)
                        {
                            if (ImGui::MenuItem("Open Script") && onNativeScriptAssetActivated)
                                onNativeScriptAssetActivated(assetKey);
                            if (hasPairedScriptFile)
                                ImGui::Separator();
                        }

                        if (isScene)
                        {
                            if (ImGui::MenuItem("Open Scene") && onSceneActivated)
                            {
                                onSceneActivated(assetKey);
                            }
                            if (ImGui::MenuItem("Set As Default Scene") && onSetDefaultSceneRequested)
                            {
                                onSetDefaultSceneRequested(assetKey);
                            }
                            ImGui::Separator();
                        }
                        if (isPrefab)
                        {
                            if (ImGui::MenuItem("Open Prefab") && onPrefabOpened)
                                onPrefabOpened(assetKey);
                            if (ImGui::MenuItem("Instantiate Prefab") && onPrefabInstantiated)
                                onPrefabInstantiated(assetKey);
                            ImGui::Separator();
                        }

                        if (ImGui::MenuItem(hasPairedScriptFile ? "Rename Script Pair" : "Rename"))
                        {
                            state.RenameAssetRelativePath = entryRelativePath;
                            if (hasPairedScriptFile)
                                CopyTextToBuffer(state.RenameAssetBuffer, entry.AbsolutePath.stem().string().c_str());
                            else
                                CopyTextToBuffer(state.RenameAssetBuffer, displayName.c_str());
                            state.RenameAssetAsNativeScriptPair = hasPairedScriptFile;
                            state.RenameAssetPopupPending = true;
                        }
                        if (ImGui::MenuItem(hasPairedScriptFile ? "Delete Script Pair" : "Delete"))
                        {
                            const bool deleteMultiSelection =
                                state.MultiSelectedAssetKeys.size() > 1 &&
                                std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) != state.MultiSelectedAssetKeys.end();
                            const bool removed = deleteMultiSelection
                                ? DeleteAssetKeysWithSceneHandling(assetsDirectory, state.MultiSelectedAssetKeys, onDeleteSceneAssetsRequested)
                                : DeleteAssetKeysWithSceneHandling(assetsDirectory, { assetKey }, onDeleteSceneAssetsRequested);
                            if (removed)
                            {
                                state.MultiSelectedAssetKeys.clear();
                                state.SelectionAnchorAssetKey.clear();
                                state.MultiSelectedSubSpriteKeys.clear();
                                state.SubSpriteSelectionAnchorKey.clear();
                                clearAssetSelection();
                                LT_INFO("Deleted asset {}", assetKey);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        const bool draggingMultiSelection =
                            state.MultiSelectedAssetKeys.size() > 1 &&
                            std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), assetKey) != state.MultiSelectedAssetKeys.end();

                        if (draggingMultiSelection)
                        {
                            const std::string payloadText = EncodeAssetKeyListPayload(state.MultiSelectedAssetKeys);
                            if (!payloadText.empty())
                            {
                                ImGui::SetDragDropPayload(
                                    kAssetMultiSelectionPayload,
                                    payloadText.c_str(),
                                    static_cast<uint32_t>(payloadText.size() + 1),
                                    ImGuiCond_Once);
                            }
                            ImGui::Text("%zu assets", state.MultiSelectedAssetKeys.size());
                        }
                        else
                        {
                            if (isTexture)
                                ImGui::SetDragDropPayload(texturePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isScene)
                                ImGui::SetDragDropPayload(scenePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isPrefab)
                                ImGui::SetDragDropPayload(prefabPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isMaterial)
                                ImGui::SetDragDropPayload(materialPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isShader)
                                ImGui::SetDragDropPayload(shaderPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isAudio)
                                ImGui::SetDragDropPayload(audioPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else if (isFont)
                                ImGui::SetDragDropPayload(fontPayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            else
                                ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);

                            ImGui::Text("%s", displayName.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }

                    // Render sub-sprite children when the texture node is expanded.
                    if (hasSubSprites && treeNodeOpen)
                    {
                        const auto& spriteSettings = GetCachedSpriteImportSettings(assetKey);
                        const auto findSubSpriteSelectionIndex = [&](const std::string& key) -> int32_t {
                            std::string selectedTextureKey;
                            int32_t selectedSubIndex = -1;
                            if (!Assets::TryParseSubSpriteAssetKey(key, selectedTextureKey, selectedSubIndex))
                                return -1;
                            if (selectedTextureKey != assetKey)
                                return -1;
                            if (selectedSubIndex < 0 || selectedSubIndex >= static_cast<int32_t>(spriteSettings.SubSprites.size()))
                                return -1;
                            return selectedSubIndex;
                        };
                        for (size_t subIdx = 0; subIdx < spriteSettings.SubSprites.size(); ++subIdx)
                        {
                            const auto& sub = spriteSettings.SubSprites[subIdx];
                            const std::string subLabel = BadgePadLabel(sub.Name) + "###sub_" + assetKey + "_" + std::to_string(subIdx);
                            const std::string subSpriteKey = assetKey + "#" + std::to_string(subIdx);
                            const ImGuiTreeNodeFlags subFlags =
                                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_FramePadding;
                            const bool subSpriteIsMultiSelected =
                                std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), subSpriteKey) != state.MultiSelectedSubSpriteKeys.end();

                            const float subIndentX = ImGui::GetCursorScreenPos().x;
                            ImGui::TreeNodeEx(subLabel.c_str(), subSpriteIsMultiSelected ? (subFlags | ImGuiTreeNodeFlags_Selected) : subFlags);
                            DrawAssetTypeBadge(kBadgeSubSprite, subIndentX);

                            const bool releasedOnSubSpriteWithoutDrag =
                                ImGui::IsItemHovered() &&
                                ImGui::IsMouseReleased(0) &&
                                (ImGui::GetDragDropPayload() == nullptr);
                            if (releasedOnSubSpriteWithoutDrag)
                            {
                                const ImGuiIO& io = ImGui::GetIO();
                                const bool shiftPressed = io.KeyShift;
                                const bool controlPressed = io.KeyCtrl;

                                // Sub-sprite picks are separate from file/folder multi-selection.
                                state.MultiSelectedAssetKeys.clear();
                                state.SelectionAnchorAssetKey.clear();

                                if (shiftPressed)
                                {
                                    const int32_t anchorIndex = findSubSpriteSelectionIndex(state.SubSpriteSelectionAnchorKey);
                                    const int32_t clickedIndex = static_cast<int32_t>(subIdx);
                                    if (!controlPressed)
                                        state.MultiSelectedSubSpriteKeys.clear();

                                    if (anchorIndex >= 0)
                                    {
                                        const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                                        const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                                        for (int32_t rangeIndex = minIndex; rangeIndex <= maxIndex; ++rangeIndex)
                                        {
                                            const std::string rangeKey = assetKey + "#" + std::to_string(rangeIndex);
                                            if (std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), rangeKey) == state.MultiSelectedSubSpriteKeys.end())
                                                state.MultiSelectedSubSpriteKeys.push_back(rangeKey);
                                        }
                                    }
                                    else if (std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), subSpriteKey) == state.MultiSelectedSubSpriteKeys.end())
                                    {
                                        state.MultiSelectedSubSpriteKeys.push_back(subSpriteKey);
                                    }
                                }
                                else if (controlPressed)
                                {
                                    const auto foundIt = std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), subSpriteKey);
                                    if (foundIt != state.MultiSelectedSubSpriteKeys.end())
                                        state.MultiSelectedSubSpriteKeys.erase(foundIt);
                                    else
                                        state.MultiSelectedSubSpriteKeys.push_back(subSpriteKey);
                                }
                                else
                                {
                                    state.MultiSelectedSubSpriteKeys.clear();
                                    state.MultiSelectedSubSpriteKeys.push_back(subSpriteKey);
                                }

                                const bool clickedStillSelected =
                                    std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), subSpriteKey) != state.MultiSelectedSubSpriteKeys.end();
                                if (!state.MultiSelectedSubSpriteKeys.empty())
                                {
                                    state.SubSpriteSelectionAnchorKey = subSpriteKey;
                                    // Keep current primary asset selection (e.g. Animation Clip)
                                    // so Timeline/Inspector context does not collapse while users
                                    // sub-select frames for drag-drop.
                                    selectedTextureAssetKey = clickedStillSelected
                                        ? subSpriteKey
                                        : state.MultiSelectedSubSpriteKeys.front();
                                }
                                else
                                {
                                    state.SubSpriteSelectionAnchorKey.clear();
                                }
                            }

                            // Drag-drop source: sub-sprite virtual key.
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                            {
                                const bool draggingMultiSubSprites =
                                    state.MultiSelectedSubSpriteKeys.size() > 1 &&
                                    std::find(state.MultiSelectedSubSpriteKeys.begin(), state.MultiSelectedSubSpriteKeys.end(), subSpriteKey) != state.MultiSelectedSubSpriteKeys.end();
                                if (draggingMultiSubSprites)
                                {
                                    const std::string payloadText = EncodeAssetKeyListPayload(state.MultiSelectedSubSpriteKeys);
                                    if (!payloadText.empty())
                                    {
                                        ImGui::SetDragDropPayload(
                                            kAssetMultiSelectionPayload,
                                            payloadText.c_str(),
                                            static_cast<uint32_t>(payloadText.size() + 1),
                                            ImGuiCond_Once);
                                    }
                                    ImGui::Text("%zu sub-sprites", state.MultiSelectedSubSpriteKeys.size());
                                }
                                else
                                {
                                    ImGui::SetDragDropPayload(
                                        kSubSpritePayloadId,
                                        subSpriteKey.c_str(),
                                        static_cast<uint32_t>(subSpriteKey.size() + 1),
                                        ImGuiCond_Once);
                                    ImGui::Text("%s", sub.Name.c_str());
                                }
                                ImGui::EndDragDropSource();
                            }

                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s\n%d x %d at (%d, %d)",
                                    sub.Name.c_str(),
                                    sub.RectPixels.z, sub.RectPixels.w,
                                    sub.RectPixels.x, sub.RectPixels.y);
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }

        void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory,
                                     EditorProjectPanelState& state,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                                     const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                                     const std::function<void(const std::string&, const std::string&)>& onAssetRenamed)
        {
            if (state.FolderPopupPending == EditorProjectFolderPopup::Create)
            {
                ImGui::OpenPopup("CreateFolder");
                ImGui::SetNextWindowFocus();
                state.FolderPopupPending = EditorProjectFolderPopup::None;
                state.CreateFolderPopupOpen = true;
            }
            else if (state.FolderPopupPending == EditorProjectFolderPopup::Rename)
            {
                ImGui::OpenPopup("RenameFolder");
                ImGui::SetNextWindowFocus();
                state.FolderPopupPending = EditorProjectFolderPopup::None;
                state.RenameFolderPopupOpen = true;
            }

            if (ImGui::BeginPopupModal("CreateFolder", &state.CreateFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Folder");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("##Name",
                                                     state.FolderPopupBuffer.data(),
                                                     state.FolderPopupBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    if (state.FolderPopupBuffer[0] != '\0')
                    {
                        if (ProjectAssetOperations::CreateFolderInDirectory(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                        {
                            InvalidateProjectDirectoryCache();
                            LT_INFO("Created folder {}", state.FolderPopupBuffer.data());
                        }
                        state.CreateFolderPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateFolderPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("RenameFolder", &state.RenameFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Rename Folder");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool rename = ImGui::InputText("##Name",
                                                     state.FolderPopupBuffer.data(),
                                                     state.FolderPopupBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
                {
                    if (state.FolderPopupBuffer[0] != '\0')
                    {
                        if (ProjectAssetOperations::RenameFolderInAssets(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                        {
                            InvalidateProjectDirectoryCache();
                            LT_INFO("Renamed folder to {}", state.FolderPopupBuffer.data());
                        }
                        state.RenameFolderPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.RenameFolderPopupOpen = false;

                ImGui::EndPopup();
            }

            if (state.RenameAssetPopupPending)
            {
                ImGui::OpenPopup("RenameAsset");
                ImGui::SetNextWindowFocus();
                state.RenameAssetPopupPending = false;
                state.RenameAssetPopupOpen = true;
            }

            if (state.CreateNativeScriptPopupPending)
            {
                ImGui::OpenPopup("CreateNativeScriptAsset");
                ImGui::SetNextWindowFocus();
                state.CreateNativeScriptPopupPending = false;
                state.CreateNativeScriptPopupOpen = true;
            }

            if (state.CreateMaterialPopupPending)
            {
                ImGui::OpenPopup("CreateMaterialAsset");
                ImGui::SetNextWindowFocus();
                state.CreateMaterialPopupPending = false;
                state.CreateMaterialPopupOpen = true;
            }

            if (state.CreateTilesetPopupPending)
            {
                ImGui::OpenPopup("CreateTilesetAsset");
                ImGui::SetNextWindowFocus();
                state.CreateTilesetPopupPending = false;
                state.CreateTilesetPopupOpen = true;
            }

            if (state.CreateTilePalettePopupPending)
            {
                ImGui::OpenPopup("CreateTilePaletteAsset");
                ImGui::SetNextWindowFocus();
                state.CreateTilePalettePopupPending = false;
                state.CreateTilePalettePopupOpen = true;
            }

            if (state.CreateAudioMixerPopupPending)
            {
                ImGui::OpenPopup("CreateAudioMixerAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAudioMixerPopupPending = false;
                state.CreateAudioMixerPopupOpen = true;
            }

            if (state.CreateInputActionsPopupPending)
            {
                ImGui::OpenPopup("CreateInputActionsAsset");
                ImGui::SetNextWindowFocus();
                state.CreateInputActionsPopupPending = false;
                state.CreateInputActionsPopupOpen = true;
            }

            if (state.CreateAnimationClipPopupPending)
            {
                ImGui::OpenPopup("CreateAnimationClipAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAnimationClipPopupPending = false;
                state.CreateAnimationClipPopupOpen = true;
            }

            if (state.CreateAnimatorControllerPopupPending)
            {
                ImGui::OpenPopup("CreateAnimatorControllerAsset");
                ImGui::SetNextWindowFocus();
                state.CreateAnimatorControllerPopupPending = false;
                state.CreateAnimatorControllerPopupOpen = true;
            }

            if (ImGui::BeginPopupModal("RenameAsset", &state.RenameAssetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Rename Asset");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool rename = ImGui::InputText("##AssetName",
                                                     state.RenameAssetBuffer.data(),
                                                     state.RenameAssetBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
                {
                    if (state.RenameAssetBuffer[0] != '\0')
                    {
                        if (state.RenameAssetAsNativeScriptPair)
                        {
                            std::filesystem::path newHeaderRelativePath;
                            std::filesystem::path newSourceRelativePath;
                            if (RenameNativeScriptPairInAssets(
                                    assetsDirectory,
                                    state.RenameAssetRelativePath,
                                    state.RenameAssetBuffer.data(),
                                    newHeaderRelativePath,
                                    newSourceRelativePath))
                            {
                                if (onAssetRenamed)
                                {
                                    const std::filesystem::path oldBase = state.RenameAssetRelativePath.parent_path() / state.RenameAssetRelativePath.stem();
                                    onAssetRenamed("Assets/" + (oldBase.generic_string() + ".h"), "Assets/" + newHeaderRelativePath.generic_string());
                                    onAssetRenamed("Assets/" + (oldBase.generic_string() + ".cpp"), "Assets/" + newSourceRelativePath.generic_string());
                                }
                                LT_INFO("Renamed script pair to {}", state.RenameAssetBuffer.data());
                            }
                        }
                        else
                        {
                            const std::string oldAssetKey = "Assets/" + state.RenameAssetRelativePath.generic_string();
                            std::filesystem::path newAssetRelativePath;
                            if (ProjectAssetOperations::RenameAssetInAssets(
                                    assetsDirectory,
                                    state.RenameAssetRelativePath,
                                    state.RenameAssetBuffer.data(),
                                    &newAssetRelativePath))
                            {
                                InvalidateProjectDirectoryCache();
                                if (onAssetRenamed)
                                {
                                    const std::string newAssetKey = "Assets/" + newAssetRelativePath.generic_string();
                                    onAssetRenamed(oldAssetKey, newAssetKey);
                                }
                                LT_INFO("Renamed asset to {}", state.RenameAssetBuffer.data());
                            }
                        }
                        state.RenameAssetAsNativeScriptPair = false;
                        state.RenameAssetRelativePath.clear();
                        state.RenameAssetBuffer[0] = '\0';
                        state.RenameAssetPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    state.RenameAssetAsNativeScriptPair = false;
                    state.RenameAssetRelativePath.clear();
                    state.RenameAssetBuffer[0] = '\0';
                    state.RenameAssetPopupOpen = false;
                }

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateNativeScriptAsset", &state.CreateNativeScriptPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Native Script");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Class Name",
                                                     state.CreateNativeScriptClassNameBuffer.data(),
                                                     state.CreateNativeScriptClassNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    std::string createdScriptAssetKey;
                    std::string createError;
                    if (CreateNativeScriptPairInAssets(
                            assetsDirectory,
                            state.CreateNativeScriptParentRelativePath,
                            state.CreateNativeScriptClassNameBuffer.data(),
                            createdScriptAssetKey,
                            createError))
                    {
                        LT_INFO("Created native script {}", createdScriptAssetKey);
                        state.CreateNativeScriptPopupOpen = false;
                    }
                    else if (!createError.empty())
                    {
                        LT_WARN("Failed to create native script: {}", createError);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateNativeScriptPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateMaterialAsset", &state.CreateMaterialPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Material");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateMaterialNameBuffer.data(),
                                                     state.CreateMaterialNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateMaterialNameBuffer.data();
                    if (!requestedName.empty() && onCreateMaterialRequested)
                    {
                        onCreateMaterialRequested(state.CreateMaterialParentRelativePath, requestedName);
                        state.CreateMaterialPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateMaterialPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateTilesetAsset", &state.CreateTilesetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Tileset");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateTilesetNameBuffer.data(),
                                                     state.CreateTilesetNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateTilesetNameBuffer.data();
                    if (!requestedName.empty() && onCreateTilesetRequested)
                    {
                        onCreateTilesetRequested(state.CreateTilesetParentRelativePath, requestedName);
                        state.CreateTilesetPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateTilesetPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateTilePaletteAsset", &state.CreateTilePalettePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Tile Palette");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateTilePaletteNameBuffer.data(),
                                                     state.CreateTilePaletteNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateTilePaletteNameBuffer.data();
                    if (!requestedName.empty())
                    {
                        // Create tile palette inline (no callback needed).
                        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
                        if (rootResult.IsSuccess())
                        {
                            const std::filesystem::path targetDir = rootResult.GetValue() / "Assets" / state.CreateTilePaletteParentRelativePath;
                            std::filesystem::create_directories(targetDir);

                            std::string filename = requestedName + ".tilepalette.json";
                            std::filesystem::path filePath = targetDir / filename;
                            int suffix = 1;
                            while (std::filesystem::exists(filePath))
                            {
                                filename = requestedName + " " + std::to_string(suffix++) + ".tilepalette.json";
                                filePath = targetDir / filename;
                            }

                            Assets::TilePaletteData emptyPalette;
                            const auto writeResult = Assets::WriteTilePaletteFile(filePath, emptyPalette);
                            if (writeResult.IsSuccess())
                            {
                                const std::filesystem::path relPath = std::filesystem::relative(filePath, rootResult.GetValue());
                                const std::string assetKey = relPath.generic_string();
                                Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::TilePalette);
                                EditorTilePalettePanel::InvalidatePaletteKeyCache();
                            }
                        }
                        state.CreateTilePalettePopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateTilePalettePopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAudioMixerAsset", &state.CreateAudioMixerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Audio Mixer");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAudioMixerNameBuffer.data(),
                                                     state.CreateAudioMixerNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAudioMixerNameBuffer.data();
                    if (!requestedName.empty() && onCreateAudioMixerRequested)
                    {
                        onCreateAudioMixerRequested(state.CreateAudioMixerParentRelativePath, requestedName);
                        state.CreateAudioMixerPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAudioMixerPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateInputActionsAsset", &state.CreateInputActionsPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Input Actions");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateInputActionsNameBuffer.data(),
                                                     state.CreateInputActionsNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateInputActionsNameBuffer.data();
                    if (!requestedName.empty() && onCreateInputActionsRequested)
                    {
                        onCreateInputActionsRequested(state.CreateInputActionsParentRelativePath, requestedName);
                        state.CreateInputActionsPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateInputActionsPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAnimationClipAsset", &state.CreateAnimationClipPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Animation Clip");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAnimationClipNameBuffer.data(),
                                                     state.CreateAnimationClipNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAnimationClipNameBuffer.data();
                    if (!requestedName.empty() && onCreateAnimationClipRequested)
                    {
                        onCreateAnimationClipRequested(state.CreateAnimationClipParentRelativePath, requestedName);
                        state.CreateAnimationClipPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAnimationClipPopupOpen = false;

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("CreateAnimatorControllerAsset", &state.CreateAnimatorControllerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Create Animator Controller");
                ImGui::Separator();
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                const bool create = ImGui::InputText("Name",
                                                     state.CreateAnimatorControllerNameBuffer.data(),
                                                     state.CreateAnimatorControllerNameBuffer.size(),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
                if (ImGui::Button("Create", ImVec2(120, 0)) || create)
                {
                    const std::string requestedName = state.CreateAnimatorControllerNameBuffer.data();
                    if (!requestedName.empty() && onCreateAnimatorControllerRequested)
                    {
                        onCreateAnimatorControllerRequested(state.CreateAnimatorControllerParentRelativePath, requestedName);
                        state.CreateAnimatorControllerPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.CreateAnimatorControllerPopupOpen = false;

                ImGui::EndPopup();
            }
        }
    }

    void Draw(EditorProjectPanelState& state,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              std::string& selectedAnimationClipAssetKey,
              std::string& selectedAnimatorControllerAssetKey,
              const char* texturePayloadId,
              const char* audioPayloadId,
              const char* assetMovePayloadId,
              const char* scenePayloadId,
              const char* materialPayloadId,
              const char* prefabPayloadId,
              const char* shaderPayloadId,
              const char* fontPayloadId,
              const std::function<void(const std::string&)>& onSceneActivated,
              const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
              const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
              const std::function<void(const std::string&)>& onPrefabOpened,
              const std::function<void(const std::string&)>& onPrefabInstantiated,
              const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
              const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
              const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested,
              const std::function<void(const std::string&)>& onNativeScriptAssetActivated)
    {
        EditorPanelStyle::PushPanelVisualStyle();
        ImGui::Begin("Project");
        state.TreeExpansionStateChanged = false;
        state.BrowseLocationChanged = false;
        state.GridScaleChanged = false;
        state.HoveredFolderRelativePathForExternalDrop.clear();
        gProjectSearchFilterLower = ToLowerAscii(std::string(state.SearchBuffer.data()));
        gProjectSearchMatchCache.clear();
        state.GridScale = std::clamp(state.GridScale, 0.70f, 1.80f);

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Could not find Assets folder.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Assets directory not found.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        if (!state.ActiveFolderRelativePath.empty())
        {
            const std::filesystem::path activeFolderAbsolutePath = assetsDirectory / state.ActiveFolderRelativePath;
            if (!std::filesystem::exists(activeFolderAbsolutePath, errorCode) || !std::filesystem::is_directory(activeFolderAbsolutePath, errorCode))
            {
                state.ActiveFolderRelativePath.clear();
                state.BrowseLocationChanged = true;
            }
        }

        const auto clearProjectAssetSelection = [&]() {
            selectedTextureAssetKey.clear();
            selectedMaterialAssetKey.clear();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedInputActionsAssetKey.clear();
            selectedAnimationClipAssetKey.clear();
            selectedAnimatorControllerAssetKey.clear();
            selectedEntity = entt::null;
            cachedTextureAsset.reset();
            cachedMaterialAsset.reset();
            state.MultiSelectedAssetKeys.clear();
            state.SelectionAnchorAssetKey.clear();
            state.MultiSelectedSubSpriteKeys.clear();
            state.SubSpriteSelectionAnchorKey.clear();
        };

        const auto clearPrimaryAssetSelection = [&]() {
            selectedTextureAssetKey.clear();
            selectedMaterialAssetKey.clear();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedInputActionsAssetKey.clear();
            selectedAnimationClipAssetKey.clear();
            selectedAnimatorControllerAssetKey.clear();
            selectedEntity = entt::null;
            cachedTextureAsset.reset();
            cachedMaterialAsset.reset();
        };

        const auto setActiveFolder = [&](std::filesystem::path relativePath) {
            relativePath = relativePath.lexically_normal();
            if (relativePath == ".")
                relativePath.clear();
            if (state.ActiveFolderRelativePath != relativePath)
            {
                state.ActiveFolderRelativePath = std::move(relativePath);
                state.AssetsRootExpanded = true;
                std::filesystem::path ancestorPath;
                for (const std::filesystem::path& segment : state.ActiveFolderRelativePath)
                {
                    ancestorPath /= segment;
                    state.ExpandedFolderState[ancestorPath.generic_string()] = true;
                }
                state.BrowseLocationChanged = true;
            }
        };

        const size_t selectedCount = !state.MultiSelectedSubSpriteKeys.empty()
            ? state.MultiSelectedSubSpriteKeys.size()
            : state.MultiSelectedAssetKeys.size();

        if (ImGui::BeginPopupContextWindow("ProjectContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            state.FolderPopupParent = state.ActiveFolderRelativePath;
            if (ImGui::MenuItem("Create Folder"))
            {
                state.FolderPopupPending = EditorProjectFolderPopup::Create;
                CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
            }
            if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                onCreateSceneRequested(state.ActiveFolderRelativePath);
            if (ImGui::MenuItem("Create Material"))
            {
                state.CreateMaterialParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                state.CreateMaterialPopupPending = true;
            }
            if (ImGui::MenuItem("Create Tileset"))
            {
                state.CreateTilesetParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
                state.CreateTilesetPopupPending = true;
            }
            if (ImGui::MenuItem("Create Tile Palette"))
            {
                state.CreateTilePaletteParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateTilePaletteNameBuffer, "New Tile Palette");
                state.CreateTilePalettePopupPending = true;
            }
            if (ImGui::MenuItem("Create Audio Mixer"))
            {
                state.CreateAudioMixerParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                state.CreateAudioMixerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Input Actions"))
            {
                state.CreateInputActionsParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateInputActionsNameBuffer, "New Input Actions");
                state.CreateInputActionsPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animation Clip"))
            {
                state.CreateAnimationClipParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                state.CreateAnimationClipPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animator Controller"))
            {
                state.CreateAnimatorControllerParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                state.CreateAnimatorControllerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Native Script"))
            {
                state.CreateNativeScriptParentRelativePath = state.ActiveFolderRelativePath;
                CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                state.CreateNativeScriptPopupPending = true;
            }
            ImGui::EndPopup();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 18.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.15f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.26f, 0.36f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.13f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.17f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.22f, 0.32f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.33f, 0.48f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.27f, 0.38f, 0.56f, 0.95f));

        if (ImGui::BeginChild("##ProjectToolbar", ImVec2(0.0f, 112.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::TextUnformatted("Project Assets");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.98f, 1.0f), "(%zu selected)", selectedCount);

            ImGui::TextColored(ImVec4(0.63f, 0.68f, 0.78f, 1.0f),
                               gProjectSearchFilterLower.empty() ? "Organize, rename, drag, and right-click to manage assets." : "Filter active: showing matching assets and folders.");

            if (ImGui::Button("Refresh"))
                InvalidateProjectDirectoryCache();
            ImGui::SameLine();
            if (ImGui::Button("Collapse Folders"))
            {
                for (auto& [folderKey, expanded] : state.ExpandedFolderState)
                {
                    (void)folderKey;
                    expanded = false;
                }
                state.AssetsRootExpanded = true;
                state.TreeExpansionStateChanged = true;
            }
            ImGui::SameLine();
            if (gProjectSearchFilterLower.empty())
                ImGui::SetNextItemWidth(-1.0f);
            else
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
            ImGui::InputTextWithHint("##ProjectSearch", "Filter assets by name or path...", state.SearchBuffer.data(), state.SearchBuffer.size());
            if (!gProjectSearchFilterLower.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                {
                    state.SearchBuffer[0] = '\0';
                    gProjectSearchFilterLower.clear();
                    gProjectSearchMatchCache.clear();
                }
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Grid Scale", &state.GridScale, 0.70f, 1.80f, "%.2fx"))
            {
                state.GridScale = std::clamp(state.GridScale, 0.70f, 1.80f);
                state.GridScaleChanged = true;
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        const bool searchActive = !gProjectSearchFilterLower.empty();

        struct ProjectGridEntry
        {
            ProjectAssetTreeEntry Entry;
            std::string DisplayName;
            std::string PrimaryAssetKey;
            std::string SecondaryAssetKey;
            const AssetTypeBadgeInfo* Badge = &kBadgeUnknown;
            bool IsDirectory = false;
            bool IsTexture = false;
            bool IsScene = false;
            bool IsMaterial = false;
            bool IsTileset = false;
            bool IsTilePalette = false;
            bool IsAudioMixer = false;
            bool IsInputActions = false;
            bool IsAnimationClip = false;
            bool IsAnimatorController = false;
            bool IsPrefab = false;
            bool IsShader = false;
            bool IsAudio = false;
            bool IsFont = false;
            bool IsNativeScriptFile = false;
            bool IsScriptPair = false;
            bool HasPairedScriptFile = false;
            bool HasThumbnailSubRect = false;
            glm::ivec4 ThumbnailRectPixels = glm::ivec4(0);
            ImVec2 ThumbnailUvMin = ImVec2(0.0f, 1.0f);
            ImVec2 ThumbnailUvMax = ImVec2(1.0f, 0.0f);
        };

        const auto resolveGridBadge = [&](const ProjectGridEntry& entry) -> const AssetTypeBadgeInfo& {
            if (entry.IsDirectory) return kBadgeFolder;
            if (entry.IsTexture) return kBadgeTexture;
            if (entry.IsScene) return kBadgeScene;
            if (entry.IsMaterial) return kBadgeMaterial;
            if (entry.IsTileset) return kBadgeTileset;
            if (entry.IsTilePalette) return kBadgeTilePalette;
            if (entry.IsAudioMixer) return kBadgeAudioMixer;
            if (entry.IsInputActions) return kBadgeInputActions;
            if (entry.IsAnimationClip) return kBadgeAnimationClip;
            if (entry.IsAnimatorController) return kBadgeAnimController;
            if (entry.IsPrefab) return kBadgePrefab;
            if (entry.IsShader) return kBadgeShader;
            if (entry.IsAudio) return kBadgeAudio;
            if (entry.IsFont) return kBadgeFont;
            if (entry.IsNativeScriptFile) return kBadgeScript;
            return kBadgeUnknown;
        };

        const auto isGridEntryPrimarySelected = [&](const ProjectGridEntry& entry) -> bool {
            std::string selectedTextureParentKey;
            int32_t selectedTextureSubIndex = -1;
            const bool selectedTextureIsSubSprite =
                Assets::TryParseSubSpriteAssetKey(selectedTextureAssetKey, selectedTextureParentKey, selectedTextureSubIndex);
            (void)selectedTextureSubIndex;

            if (entry.IsTexture)
                return selectedTextureAssetKey == entry.PrimaryAssetKey || (selectedTextureIsSubSprite && selectedTextureParentKey == entry.PrimaryAssetKey);
            if (entry.IsMaterial)
                return selectedMaterialAssetKey == entry.PrimaryAssetKey;
            if (entry.IsTileset)
                return selectedTilesetAssetKey == entry.PrimaryAssetKey;
            if (entry.IsAudioMixer)
                return selectedAudioMixerAssetKey == entry.PrimaryAssetKey;
            if (entry.IsInputActions)
                return selectedInputActionsAssetKey == entry.PrimaryAssetKey;
            if (entry.IsAnimationClip)
                return selectedAnimationClipAssetKey == entry.PrimaryAssetKey;
            if (entry.IsAnimatorController)
                return selectedAnimatorControllerAssetKey == entry.PrimaryAssetKey;
            if (entry.IsNativeScriptFile)
                return selectedNativeScriptAssetKey == entry.PrimaryAssetKey || (!entry.SecondaryAssetKey.empty() && selectedNativeScriptAssetKey == entry.SecondaryAssetKey);
            if (entry.IsPrefab)
                return selectedPrefabAssetKey == entry.PrimaryAssetKey;
            return false;
        };

        const auto setPrimarySelectionForGridEntry = [&](const ProjectGridEntry& entry) {
            clearPrimaryAssetSelection();
            if (entry.IsTexture)
                selectedTextureAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsMaterial)
                selectedMaterialAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsNativeScriptFile)
                selectedNativeScriptAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsPrefab)
                selectedPrefabAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsTileset)
                selectedTilesetAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsAudioMixer)
                selectedAudioMixerAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsInputActions)
                selectedInputActionsAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsAnimationClip)
                selectedAnimationClipAssetKey = entry.PrimaryAssetKey;
            else if (entry.IsAnimatorController)
                selectedAnimatorControllerAssetKey = entry.PrimaryAssetKey;
        };

        const auto beginFolderContextMenu = [&](const std::filesystem::path& folderRelativePath, const std::string& folderName, const bool isRoot) {
            if (!ImGui::BeginPopupContextItem())
                return;

            state.FolderPopupParent = folderRelativePath;
            if (ImGui::MenuItem("Create Folder"))
            {
                state.FolderPopupPending = EditorProjectFolderPopup::Create;
                CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
            }
            if (ImGui::MenuItem("Create Scene") && onCreateSceneRequested)
                onCreateSceneRequested(folderRelativePath);
            if (ImGui::MenuItem("Create Material"))
            {
                state.CreateMaterialParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateMaterialNameBuffer, "New Material");
                state.CreateMaterialPopupPending = true;
            }
            if (ImGui::MenuItem("Create Tileset"))
            {
                state.CreateTilesetParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateTilesetNameBuffer, "New Tileset");
                state.CreateTilesetPopupPending = true;
            }
            if (ImGui::MenuItem("Create Tile Palette"))
            {
                state.CreateTilePaletteParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateTilePaletteNameBuffer, "New Tile Palette");
                state.CreateTilePalettePopupPending = true;
            }
            if (ImGui::MenuItem("Create Audio Mixer"))
            {
                state.CreateAudioMixerParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateAudioMixerNameBuffer, "New Audio Mixer");
                state.CreateAudioMixerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Input Actions"))
            {
                state.CreateInputActionsParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateInputActionsNameBuffer, "New Input Actions");
                state.CreateInputActionsPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animation Clip"))
            {
                state.CreateAnimationClipParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateAnimationClipNameBuffer, "New Animation Clip");
                state.CreateAnimationClipPopupPending = true;
            }
            if (ImGui::MenuItem("Create Animator Controller"))
            {
                state.CreateAnimatorControllerParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateAnimatorControllerNameBuffer, "New Animator Controller");
                state.CreateAnimatorControllerPopupPending = true;
            }
            if (ImGui::MenuItem("Create Native Script"))
            {
                state.CreateNativeScriptParentRelativePath = folderRelativePath;
                CopyTextToBuffer(state.CreateNativeScriptClassNameBuffer, "NewNativeScript");
                state.CreateNativeScriptPopupPending = true;
            }

            if (!isRoot)
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Rename"))
                {
                    state.FolderPopupPending = EditorProjectFolderPopup::Rename;
                    CopyTextToBuffer(state.FolderPopupBuffer, folderName.c_str());
                }
                if (ImGui::MenuItem("Delete"))
                {
                    if (ProjectAssetOperations::DeleteFolderInAssets(assetsDirectory, folderRelativePath))
                    {
                        InvalidateProjectDirectoryCache();
                        if (state.ActiveFolderRelativePath == folderRelativePath ||
                            (!state.ActiveFolderRelativePath.empty() && state.ActiveFolderRelativePath.generic_string().rfind(folderRelativePath.generic_string() + "/", 0) == 0))
                        {
                            setActiveFolder(folderRelativePath.parent_path());
                        }
                        LT_INFO("Deleted folder {}", folderRelativePath.generic_string());
                    }
                }
            }

            ImGui::EndPopup();
        };

        const auto acceptFolderDropTarget = [&](const std::filesystem::path& folderRelativePath) {
            if (!ImGui::BeginDragDropTarget())
                return;

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMultiSelectionPayload))
            {
                const std::vector<std::string> keys = ParseAssetKeyListPayload(payload->Data, payload->DataSize);
                MoveAssetListToTargetFolder(keys, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                MoveAssetOrFolderToTargetFolder(key, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                MoveAssetOrFolderToTargetFolder(key, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                MoveAssetOrFolderToTargetFolder(key, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                MoveAssetOrFolderToTargetFolder(key, folderRelativePath);
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
            {
                const auto* entity = static_cast<const entt::entity*>(payload->Data);
                if (entity && onCreatePrefabFromSceneEntityRequested)
                    onCreatePrefabFromSceneEntityRequested(*entity, folderRelativePath);
            }

            ImGui::EndDragDropTarget();
        };

        std::function<void(const std::filesystem::path&, bool)> drawFolderNode = [&](const std::filesystem::path& relativePath, const bool isRoot) {
            const std::string displayName = isRoot ? std::string("Assets") : relativePath.filename().string();
            const std::string folderStateKey = relativePath.generic_string();
            const bool selected = relativePath == state.ActiveFolderRelativePath;
            bool folderExpanded = isRoot ? state.AssetsRootExpanded : true;
            if (!isRoot)
            {
                if (const auto expandedStateIt = state.ExpandedFolderState.find(folderStateKey); expandedStateIt != state.ExpandedFolderState.end())
                    folderExpanded = expandedStateIt->second;
            }

            const bool previousFolderExpanded = folderExpanded;
            ImGui::SetNextItemOpen(folderExpanded, ImGuiCond_Always);
            const float folderIndentX = ImGui::GetCursorScreenPos().x;
            const std::string label = isRoot
                ? (BadgePadLabel(displayName) + "###ProjectFolderRoot")
                : (BadgePadLabel(displayName) + "###ProjectFolder_" + folderStateKey);
            const bool nodeOpen = ImGui::TreeNodeEx(label.c_str(),
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding |
                (selected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None));
            DrawAssetTypeBadge(kBadgeFolder, folderIndentX);

            if (isRoot)
            {
                state.AssetsRootExpanded = nodeOpen;
                if (state.AssetsRootExpanded != previousFolderExpanded)
                    state.TreeExpansionStateChanged = true;
            }
            else
            {
                state.ExpandedFolderState[folderStateKey] = nodeOpen;
                if (nodeOpen != previousFolderExpanded)
                    state.TreeExpansionStateChanged = true;
            }

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                setActiveFolder(relativePath);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                state.HoveredFolderRelativePathForExternalDrop = relativePath;

            beginFolderContextMenu(relativePath, displayName, isRoot);
            acceptFolderDropTarget(relativePath);

            if (!isRoot && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                const std::string assetKey = "Assets/" + relativePath.generic_string();
                ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                ImGui::Text("%s", displayName.c_str());
                ImGui::EndDragDropSource();
            }

            if (nodeOpen)
            {
                const std::vector<ProjectAssetTreeEntry>& childEntries = GetCachedProjectDirectoryEntries(assetsDirectory, relativePath);
                for (const ProjectAssetTreeEntry& childEntry : childEntries)
                {
                    if (childEntry.IsDirectory)
                        drawFolderNode(childEntry.RelativePath, false);
                }
                ImGui::TreePop();
            }
        };

        std::function<void(const std::filesystem::path&, std::vector<ProjectGridEntry>&)> appendGridEntries =
            [&](const std::filesystem::path& relativePath, std::vector<ProjectGridEntry>& output) {
                const std::vector<ProjectAssetTreeEntry>& entries = GetCachedProjectDirectoryEntries(assetsDirectory, relativePath);
                std::unordered_map<std::string, uint8_t> scriptPairPresenceByBasePath;
                scriptPairPresenceByBasePath.reserve(entries.size());
                for (const ProjectAssetTreeEntry& entry : entries)
                {
                    if (entry.IsDirectory || !IsNativeScriptExtensionLower(entry.LowerExtension))
                        continue;
                    const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / std::filesystem::path(entry.FileName).stem();
                    const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                    uint8_t& presenceBits = scriptPairPresenceByBasePath[scriptBaseKey];
                    if (entry.LowerExtension == ".h")
                        presenceBits |= kScriptPairHeaderBit;
                    else if (entry.LowerExtension == ".cpp")
                        presenceBits |= kScriptPairSourceBit;
                }

                std::unordered_set<std::string> renderedScriptBasePaths;
                for (const ProjectAssetTreeEntry& entry : entries)
                {
                    if (entry.IsDirectory)
                    {
                        const bool directoryVisible = !searchActive || MatchesProjectSearchFilter(entry.FileName) || DirectoryContainsProjectSearchMatch(assetsDirectory, entry.RelativePath);
                        if (!directoryVisible)
                            continue;

                        ProjectGridEntry folderEntry;
                        folderEntry.Entry = entry;
                        folderEntry.DisplayName = entry.FileName;
                        folderEntry.PrimaryAssetKey = entry.AssetKey;
                        folderEntry.Badge = &kBadgeFolder;
                        folderEntry.IsDirectory = true;
                        output.push_back(std::move(folderEntry));

                        if (searchActive)
                            appendGridEntries(entry.RelativePath, output);
                        continue;
                    }

                    if (IsNativeScriptExtensionLower(entry.LowerExtension))
                    {
                        const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / entry.RelativePath.stem();
                        const std::string scriptBaseKey = scriptBaseRelativePath.generic_string();
                        if (renderedScriptBasePaths.find(scriptBaseKey) != renderedScriptBasePaths.end())
                            continue;

                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseKey);
                        const bool hasScriptPair =
                            scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                            (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                (kScriptPairHeaderBit | kScriptPairSourceBit);
                        if (hasScriptPair)
                        {
                            const std::filesystem::path headerRelativePath = scriptBaseRelativePath.string() + ".h";
                            const std::filesystem::path sourceRelativePath = scriptBaseRelativePath.string() + ".cpp";
                            const std::string sourceAssetKey = "Assets/" + sourceRelativePath.generic_string();
                            const std::string headerAssetKey = "Assets/" + headerRelativePath.generic_string();
                            const std::string scriptBaseName = scriptBaseRelativePath.stem().string();
                            const bool matchesSearch = !searchActive ||
                                MatchesProjectSearchFilter(scriptBaseName) ||
                                MatchesProjectSearchFilter(sourceAssetKey) ||
                                MatchesProjectSearchFilter(headerAssetKey);
                            renderedScriptBasePaths.insert(scriptBaseKey);
                            if (!matchesSearch)
                                continue;

                            ProjectGridEntry scriptEntry;
                            scriptEntry.Entry = entry;
                            scriptEntry.DisplayName = scriptBaseName + " [Native Script]";
                            scriptEntry.PrimaryAssetKey = sourceAssetKey;
                            scriptEntry.SecondaryAssetKey = headerAssetKey;
                            scriptEntry.Badge = &kBadgeScript;
                            scriptEntry.IsNativeScriptFile = true;
                            scriptEntry.IsScriptPair = true;
                            scriptEntry.HasPairedScriptFile = true;
                            output.push_back(std::move(scriptEntry));
                            continue;
                        }
                    }

                    const bool isTexture = IsTextureExtensionLower(entry.LowerExtension);
                    const bool isScene = IsSceneFileNameLower(entry.LowerFileName);
                    const bool isMaterial = IsMaterialFileNameLower(entry.LowerFileName);
                    const bool isTileset = IsTilesetFileNameLower(entry.LowerFileName);
                    const bool isTilePalette = IsTilePaletteFileNameLower(entry.LowerFileName);
                    const bool isAudioMixer = IsAudioMixerFileNameLower(entry.LowerFileName);
                    const bool isInputActions = IsInputActionsFileNameLower(entry.LowerFileName);
                    const bool isAnimationClip = IsAnimationClipFileNameLower(entry.LowerFileName);
                    const bool isAnimatorController = IsAnimatorControllerFileNameLower(entry.LowerFileName);
                    const bool isPrefab = IsPrefabFileNameLower(entry.LowerFileName);
                    const bool isShader = IsShaderExtensionLower(entry.LowerExtension);
                    const bool isAudio = IsAudioExtensionLower(entry.LowerExtension);
                    const bool isFont = IsFontExtensionLower(entry.LowerExtension);
                    const bool isNativeScriptFile = IsNativeScriptExtensionLower(entry.LowerExtension);
                    const bool hasPairedScriptFile = isNativeScriptFile && ([&]() {
                        const std::filesystem::path scriptBaseRelativePath = entry.RelativePath.parent_path() / entry.RelativePath.stem();
                        const auto scriptPairPresenceIt = scriptPairPresenceByBasePath.find(scriptBaseRelativePath.generic_string());
                        return scriptPairPresenceIt != scriptPairPresenceByBasePath.end() &&
                               (scriptPairPresenceIt->second & (kScriptPairHeaderBit | kScriptPairSourceBit)) ==
                                   (kScriptPairHeaderBit | kScriptPairSourceBit);
                    })();
                    const std::string displayName = isNativeScriptFile
                        ? BuildScriptAssetDisplayName(entry.AbsolutePath)
                        : GetAssetDisplayName(entry.AbsolutePath);
                    if (searchActive && !EntryMatchesProjectSearchFilter(entry, displayName))
                        continue;

                    ProjectGridEntry fileEntry;
                    fileEntry.Entry = entry;
                    fileEntry.DisplayName = displayName;
                    fileEntry.PrimaryAssetKey = entry.AssetKey;
                    fileEntry.IsTexture = isTexture;
                    fileEntry.IsScene = isScene;
                    fileEntry.IsMaterial = isMaterial;
                    fileEntry.IsTileset = isTileset;
                    fileEntry.IsTilePalette = isTilePalette;
                    fileEntry.IsAudioMixer = isAudioMixer;
                    fileEntry.IsInputActions = isInputActions;
                    fileEntry.IsAnimationClip = isAnimationClip;
                    fileEntry.IsAnimatorController = isAnimatorController;
                    fileEntry.IsPrefab = isPrefab;
                    fileEntry.IsShader = isShader;
                    fileEntry.IsAudio = isAudio;
                    fileEntry.IsFont = isFont;
                    fileEntry.IsNativeScriptFile = isNativeScriptFile;
                    fileEntry.HasPairedScriptFile = hasPairedScriptFile;
                    if (isTexture)
                    {
                        const Assets::SpriteImportSettings& spriteSettings = GetCachedSpriteImportSettings(entry.AssetKey);
                        if (spriteSettings.Mode == Assets::SpriteImportSettings::SpriteMode::Multiple && !spriteSettings.SubSprites.empty())
                        {
                            const auto& firstSubSprite = spriteSettings.SubSprites.front();
                            fileEntry.HasThumbnailSubRect = true;
                            fileEntry.ThumbnailRectPixels = firstSubSprite.RectPixels;
                        }
                    }
                    fileEntry.Badge = &resolveGridBadge(fileEntry);
                    output.push_back(std::move(fileEntry));
                }
            };

        ImGui::BeginChild("##ProjectBrowserRegion", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::BeginChild("##ProjectFolderPane", ImVec2(260.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::TextUnformatted("Folders");
            ImGui::Separator();
            drawFolderNode("", true);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("##ProjectGridPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
                state.HoveredFolderRelativePathForExternalDrop = state.ActiveFolderRelativePath;

            if (!state.ActiveFolderRelativePath.empty())
            {
                if (ImGui::Button("Up"))
                    setActiveFolder(state.ActiveFolderRelativePath.parent_path());
                ImGui::SameLine();
            }

            if (ImGui::SmallButton("Assets"))
                setActiveFolder("");

            const std::filesystem::path activeFolderPathForBreadcrumbs = state.ActiveFolderRelativePath;
            std::optional<std::filesystem::path> pendingBreadcrumbFolder;
            std::filesystem::path breadcrumbPath;
            for (const std::filesystem::path& segment : activeFolderPathForBreadcrumbs)
            {
                breadcrumbPath /= segment;
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
                ImGui::PushID(breadcrumbPath.generic_string().c_str());
                if (ImGui::SmallButton(segment.string().c_str()))
                    pendingBreadcrumbFolder = breadcrumbPath;
                ImGui::PopID();
            }

            if (pendingBreadcrumbFolder.has_value())
                setActiveFolder(*pendingBreadcrumbFolder);

            ImGui::Separator();

            std::vector<ProjectGridEntry> gridEntries;
            appendGridEntries(state.ActiveFolderRelativePath, gridEntries);

            std::vector<std::string> visibleAssetKeys;
            visibleAssetKeys.reserve(gridEntries.size());
            for (const ProjectGridEntry& entry : gridEntries)
            {
                if (!entry.IsDirectory && !entry.PrimaryAssetKey.empty())
                    visibleAssetKeys.push_back(entry.PrimaryAssetKey);
            }

            if (gridEntries.empty())
            {
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.90f, 1.0f),
                    searchActive ? "No assets match the current filter in this location." : "This folder is empty.");
            }
            else
            {
                const float gridScale = state.GridScale;
                const float tileWidth = 168.0f * gridScale;
                const float tileSpacing = 14.0f * gridScale;
                const float previewInset = 10.0f * gridScale;
                const float previewTopOffset = 24.0f * gridScale;
                const float previewHeight = 96.0f * gridScale;
                const float badgeOffsetX = 10.0f * gridScale;
                const float badgeOffsetY = 10.0f * gridScale;
                const float badgePadX = 7.0f * gridScale;
                const float badgePadY = 4.0f * gridScale;
                const float tileTextPadX = 12.0f * gridScale;
                const float textBlockTopPadding = 10.0f * gridScale;
                const float textLineGap = std::max(1.0f, 2.0f * gridScale);
                const float textBlockBottomPadding = 12.0f * gridScale;
                const float badgeFontSize = ImGui::GetFontSize();
                const float nameLineHeight = ImGui::GetTextLineHeight();
                const float pathLineHeight = ImGui::GetTextLineHeight();
                const float tileHeight =
                    previewInset + previewTopOffset + previewHeight + textBlockTopPadding +
                    nameLineHeight + textLineGap + pathLineHeight + textBlockBottomPadding;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const int columns = std::max(1, static_cast<int>((availableWidth + tileSpacing) / (tileWidth + tileSpacing)));
                const auto fitTextToWidth = [](const std::string& text, const float maxWidth) {
                    if (text.empty() || maxWidth <= 0.0f)
                        return std::string{};

                    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
                        return text;

                    constexpr const char* ellipsis = "...";
                    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
                    if (ellipsisWidth >= maxWidth)
                        return std::string(ellipsis);

                    std::string fitted = text;
                    while (!fitted.empty())
                    {
                        fitted.pop_back();
                        const std::string candidate = fitted + ellipsis;
                        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
                            return candidate;
                    }

                    return std::string(ellipsis);
                };

                for (size_t index = 0; index < gridEntries.size(); ++index)
                {
                    ProjectGridEntry& entry = gridEntries[index];
                    if (index > 0 && static_cast<int>(index % static_cast<size_t>(columns)) != 0)
                        ImGui::SameLine(0.0f, tileSpacing);

                    ImGui::PushID(static_cast<int>(index));
                    const ImVec2 tileMin = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("##AssetTile", ImVec2(tileWidth, tileHeight));
                    const bool hovered = ImGui::IsItemHovered();
                    const bool releasedOnItemWithoutDrag =
                        hovered && ImGui::IsMouseReleased(0) && (ImGui::GetDragDropPayload() == nullptr);
                    const bool isMultiSelected = !entry.IsDirectory &&
                        std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
                    const bool isSelected = !entry.IsDirectory && (isGridEntryPrimarySelected(entry) || isMultiSelected);

                    const ImVec2 tileMax(tileMin.x + tileWidth, tileMin.y + tileHeight);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const auto drawCrispText = [&](const ImVec2& pos, const ImU32 color, const std::string& text) {
                        drawList->AddText(pos, color, text.c_str());
                    };
                    const ImU32 fillColor = isSelected
                        ? IM_COL32(44, 79, 138, 240)
                        : (hovered ? IM_COL32(29, 39, 60, 240) : IM_COL32(19, 26, 40, 225));
                    const ImU32 borderColor = isSelected
                        ? IM_COL32(92, 145, 230, 255)
                        : IM_COL32(56, 72, 104, 210);
                    drawList->AddRectFilled(tileMin, tileMax, fillColor, 8.0f);
                    drawList->AddRect(tileMin, tileMax, borderColor, 8.0f, 0, isSelected ? 2.0f : 1.0f);

                    const ImVec2 previewMin(tileMin.x + previewInset, tileMin.y + previewInset + previewTopOffset);
                    const ImVec2 previewMax(tileMax.x - previewInset, previewMin.y + previewHeight);
                    drawList->AddRectFilled(previewMin, previewMax, IM_COL32(13, 18, 29, 240), 6.0f);
                    drawList->AddRect(previewMin, previewMax, IM_COL32(48, 61, 90, 220), 6.0f, 0, 1.0f);

                    const AssetTypeBadgeInfo& badge = *entry.Badge;
                    const ImVec2 badgeTextSize = ImGui::GetFont()->CalcTextSizeA(badgeFontSize, 100000.0f, 0.0f, badge.Label);
                    const ImVec2 badgeMin(tileMin.x + badgeOffsetX, tileMin.y + badgeOffsetY);
                    const ImVec2 badgeMax(badgeMin.x + badgeTextSize.x + badgePadX * 2.0f, badgeMin.y + badgeTextSize.y + badgePadY * 2.0f);
                    drawList->AddRectFilled(badgeMin, badgeMax, badge.FillColor, 5.0f);
                    drawList->AddRect(badgeMin, badgeMax, badge.BorderColor, 5.0f, 0, 1.0f);
                    drawList->AddText(ImVec2(badgeMin.x + badgePadX, badgeMin.y + badgePadY), badge.TextColor, badge.Label);

                    if (entry.IsTexture)
                    {
                        if (Assets::TextureAsset::Ptr textureAsset = GetCachedThumbnailTextureAsset(entry.PrimaryAssetKey))
                        {
                            if (const auto& textureHandle = textureAsset->GetTexture(); textureHandle)
                            {
                                const float textureWidth = static_cast<float>(std::max(1u, textureHandle->GetWidth()));
                                const float textureHeight = static_cast<float>(std::max(1u, textureHandle->GetHeight()));
                                ImVec2 uvMin = entry.ThumbnailUvMin;
                                ImVec2 uvMax = entry.ThumbnailUvMax;
                                if (entry.HasThumbnailSubRect)
                                {
                                    const glm::vec4 subUvs = Assets::ComputeSubSpriteUvs(
                                        entry.ThumbnailRectPixels,
                                        textureHandle->GetWidth(),
                                        textureHandle->GetHeight());
                                    uvMin = ImVec2(subUvs.x, 1.0f - subUvs.y);
                                    uvMax = ImVec2(subUvs.z, 1.0f - subUvs.w);
                                }
                                const float previewWidth = previewMax.x - previewMin.x;
                                const float previewInnerHeight = previewMax.y - previewMin.y;
                                const float scale = std::min(previewWidth / textureWidth, previewInnerHeight / textureHeight);
                                const float drawWidth = std::max(1.0f, textureWidth * scale);
                                const float drawHeight = std::max(1.0f, textureHeight * scale);
                                const ImVec2 imageMin(
                                    previewMin.x + (previewWidth - drawWidth) * 0.5f,
                                    previewMin.y + (previewInnerHeight - drawHeight) * 0.5f);
                                const ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);
                                drawList->AddImage(
                                    static_cast<ImTextureID>(GetTextureNativeHandle(textureHandle)),
                                    imageMin,
                                    imageMax,
                                    uvMin,
                                    uvMax,
                                    IM_COL32_WHITE);
                            }
                        }
                    }
                    else if (entry.IsDirectory)
                    {
                        const char* folderGlyph = "DIR";
                        const ImVec2 glyphSize = ImGui::CalcTextSize(folderGlyph);
                        drawList->AddText(
                            ImVec2(previewMin.x + (previewMax.x - previewMin.x - glyphSize.x) * 0.5f,
                                   previewMin.y + (previewMax.y - previewMin.y - glyphSize.y) * 0.5f),
                            IM_COL32(130, 170, 235, 255),
                            folderGlyph);
                    }
                    else
                    {
                        drawList->AddText(ImVec2(previewMin.x + tileTextPadX, previewMin.y + tileTextPadX), badge.TextColor, badge.Label);
                    }

                    const std::string secondaryText = entry.IsDirectory
                        ? (entry.Entry.RelativePath.empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.generic_string())
                        : (entry.Entry.RelativePath.parent_path().empty() ? std::string("Assets") : std::string("Assets/") + entry.Entry.RelativePath.parent_path().generic_string());
                    const float availableTextWidth = std::max(0.0f, tileWidth - tileTextPadX * 2.0f);
                    const std::string fittedDisplayName = fitTextToWidth(entry.DisplayName, availableTextWidth);
                    const std::string fittedSecondaryText = fitTextToWidth(secondaryText, availableTextWidth);
                    const float nameTextY = previewMax.y + textBlockTopPadding;
                    const float pathTextY = nameTextY + nameLineHeight + textLineGap;
                    drawList->PushClipRect(tileMin, tileMax, true);
                    drawCrispText(ImVec2(tileMin.x + tileTextPadX, nameTextY), IM_COL32(230, 236, 245, 255), fittedDisplayName);
                    drawCrispText(ImVec2(tileMin.x + tileTextPadX, pathTextY), IM_COL32(145, 156, 176, 255), fittedSecondaryText);
                    drawList->PopClipRect();

                    if (entry.IsDirectory)
                    {
                        if (hovered)
                            state.HoveredFolderRelativePathForExternalDrop = entry.Entry.RelativePath;

                        if (hovered && ImGui::IsMouseDoubleClicked(0))
                            setActiveFolder(entry.Entry.RelativePath);

                        beginFolderContextMenu(entry.Entry.RelativePath, entry.Entry.FileName, false);
                        acceptFolderDropTarget(entry.Entry.RelativePath);

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                        {
                            const std::string assetKey = entry.PrimaryAssetKey;
                            ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                            ImGui::Text("%s", entry.DisplayName.c_str());
                            ImGui::EndDragDropSource();
                        }
                    }
                    else
                    {
                        if (releasedOnItemWithoutDrag)
                        {
                            const ImGuiIO& io = ImGui::GetIO();
                            const bool shiftPressed = io.KeyShift;
                            const bool controlPressed = io.KeyCtrl;
                            state.MultiSelectedSubSpriteKeys.clear();
                            state.SubSpriteSelectionAnchorKey.clear();

                            if (shiftPressed)
                            {
                                const auto findIndex = [&](const std::string& key) -> int32_t {
                                    for (size_t selectionIndex = 0; selectionIndex < visibleAssetKeys.size(); ++selectionIndex)
                                    {
                                        if (visibleAssetKeys[selectionIndex] == key)
                                            return static_cast<int32_t>(selectionIndex);
                                    }
                                    return -1;
                                };

                                const int32_t anchorIndex = findIndex(state.SelectionAnchorAssetKey);
                                const int32_t clickedIndex = findIndex(entry.PrimaryAssetKey);
                                if (!controlPressed)
                                    state.MultiSelectedAssetKeys.clear();

                                if (anchorIndex >= 0 && clickedIndex >= 0)
                                {
                                    const int32_t minIndex = std::min(anchorIndex, clickedIndex);
                                    const int32_t maxIndex = std::max(anchorIndex, clickedIndex);
                                    for (int32_t rangeIndex = minIndex; rangeIndex <= maxIndex; ++rangeIndex)
                                    {
                                        const std::string& rangeKey = visibleAssetKeys[static_cast<size_t>(rangeIndex)];
                                        if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), rangeKey) == state.MultiSelectedAssetKeys.end())
                                            state.MultiSelectedAssetKeys.push_back(rangeKey);
                                    }
                                }
                                else if (std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) == state.MultiSelectedAssetKeys.end())
                                {
                                    state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
                                }

                                setPrimarySelectionForGridEntry(entry);
                            }
                            else if (controlPressed)
                            {
                                const auto foundIt = std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey);
                                const bool wasSelected = foundIt != state.MultiSelectedAssetKeys.end();
                                if (wasSelected)
                                {
                                    state.MultiSelectedAssetKeys.erase(foundIt);
                                    if (state.MultiSelectedAssetKeys.empty())
                                        clearPrimaryAssetSelection();
                                }
                                else
                                {
                                    state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
                                    setPrimarySelectionForGridEntry(entry);
                                }
                                state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            }
                            else
                            {
                                state.MultiSelectedAssetKeys.clear();
                                state.MultiSelectedAssetKeys.push_back(entry.PrimaryAssetKey);
                                state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                                setPrimarySelectionForGridEntry(entry);
                            }
                        }

                        if (entry.IsTexture && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedTextureAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsScene && hovered && ImGui::IsMouseDoubleClicked(0) && onSceneActivated)
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedNativeScriptAssetKey.clear();
                            onSceneActivated(entry.PrimaryAssetKey);
                        }
                        else if (entry.IsPrefab && hovered && ImGui::IsMouseDoubleClicked(0) && onPrefabOpened)
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedPrefabAssetKey = entry.PrimaryAssetKey;
                            onPrefabOpened(entry.PrimaryAssetKey);
                        }
                        else if (entry.IsMaterial && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedMaterialAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsTileset && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedTilesetAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsAudioMixer && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedAudioMixerAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsAnimationClip && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedAnimationClipAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsAnimatorController && hovered && ImGui::IsMouseDoubleClicked(0))
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedAnimatorControllerAssetKey = entry.PrimaryAssetKey;
                        }
                        else if (entry.IsNativeScriptFile && hovered && ImGui::IsMouseDoubleClicked(0) && onNativeScriptAssetActivated)
                        {
                            state.MultiSelectedAssetKeys = { entry.PrimaryAssetKey };
                            state.SelectionAnchorAssetKey = entry.PrimaryAssetKey;
                            clearPrimaryAssetSelection();
                            selectedNativeScriptAssetKey = entry.PrimaryAssetKey;
                            onNativeScriptAssetActivated(entry.PrimaryAssetKey);
                        }

                        if (ImGui::BeginPopupContextItem())
                        {
                            if (entry.IsNativeScriptFile)
                            {
                                if (ImGui::MenuItem("Open Script") && onNativeScriptAssetActivated)
                                    onNativeScriptAssetActivated(entry.PrimaryAssetKey);
                                if (entry.HasPairedScriptFile)
                                    ImGui::Separator();
                            }

                            if (entry.IsScene)
                            {
                                if (ImGui::MenuItem("Open Scene") && onSceneActivated)
                                    onSceneActivated(entry.PrimaryAssetKey);
                                if (ImGui::MenuItem("Set As Default Scene") && onSetDefaultSceneRequested)
                                    onSetDefaultSceneRequested(entry.PrimaryAssetKey);
                                ImGui::Separator();
                            }

                            if (entry.IsPrefab)
                            {
                                if (ImGui::MenuItem("Open Prefab") && onPrefabOpened)
                                    onPrefabOpened(entry.PrimaryAssetKey);
                                if (ImGui::MenuItem("Instantiate Prefab") && onPrefabInstantiated)
                                    onPrefabInstantiated(entry.PrimaryAssetKey);
                                ImGui::Separator();
                            }

                            if (ImGui::MenuItem(entry.HasPairedScriptFile ? "Rename Script Pair" : "Rename"))
                            {
                                state.RenameAssetRelativePath = entry.Entry.RelativePath;
                                if (entry.HasPairedScriptFile)
                                    CopyTextToBuffer(state.RenameAssetBuffer, entry.Entry.AbsolutePath.stem().string().c_str());
                                else
                                    CopyTextToBuffer(state.RenameAssetBuffer, entry.DisplayName.c_str());
                                state.RenameAssetAsNativeScriptPair = entry.HasPairedScriptFile;
                                state.RenameAssetPopupPending = true;
                            }

                            if (ImGui::MenuItem(entry.HasPairedScriptFile ? "Delete Script Pair" : "Delete"))
                            {
                                const bool deleteMultiSelection =
                                    state.MultiSelectedAssetKeys.size() > 1 &&
                                    std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
                                const bool removed = deleteMultiSelection
                                    ? DeleteAssetKeysWithSceneHandling(assetsDirectory, state.MultiSelectedAssetKeys, onDeleteSceneAssetsRequested)
                                    : DeleteAssetKeysWithSceneHandling(assetsDirectory, { entry.PrimaryAssetKey }, onDeleteSceneAssetsRequested);
                                if (removed)
                                {
                                    state.MultiSelectedAssetKeys.clear();
                                    state.SelectionAnchorAssetKey.clear();
                                    state.MultiSelectedSubSpriteKeys.clear();
                                    state.SubSpriteSelectionAnchorKey.clear();
                                    clearPrimaryAssetSelection();
                                    InvalidateProjectDirectoryCache();
                                    LT_INFO("Deleted asset {}", entry.PrimaryAssetKey);
                                }
                            }

                            ImGui::EndPopup();
                        }

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                        {
                            const bool draggingMultiSelection =
                                state.MultiSelectedAssetKeys.size() > 1 &&
                                std::find(state.MultiSelectedAssetKeys.begin(), state.MultiSelectedAssetKeys.end(), entry.PrimaryAssetKey) != state.MultiSelectedAssetKeys.end();
                            if (draggingMultiSelection)
                            {
                                const std::string payloadText = EncodeAssetKeyListPayload(state.MultiSelectedAssetKeys);
                                if (!payloadText.empty())
                                {
                                    ImGui::SetDragDropPayload(
                                        kAssetMultiSelectionPayload,
                                        payloadText.c_str(),
                                        static_cast<uint32_t>(payloadText.size() + 1),
                                        ImGuiCond_Once);
                                }
                                ImGui::Text("%zu assets", state.MultiSelectedAssetKeys.size());
                            }
                            else
                            {
                                if (entry.IsTexture)
                                    ImGui::SetDragDropPayload(texturePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsScene)
                                    ImGui::SetDragDropPayload(scenePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsPrefab)
                                    ImGui::SetDragDropPayload(prefabPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsMaterial)
                                    ImGui::SetDragDropPayload(materialPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsShader)
                                    ImGui::SetDragDropPayload(shaderPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsAudio)
                                    ImGui::SetDragDropPayload(audioPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else if (entry.IsFont)
                                    ImGui::SetDragDropPayload(fontPayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                else
                                    ImGui::SetDragDropPayload(assetMovePayloadId, entry.PrimaryAssetKey.c_str(), static_cast<uint32_t>(entry.PrimaryAssetKey.size() + 1), ImGuiCond_Once);
                                ImGui::Text("%s", entry.DisplayName.c_str());
                            }
                            ImGui::EndDragDropSource();
                        }
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();

        ImGui::EndChild();

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const bool canDeleteSelection =
                !io.WantTextInput &&
                !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
            if (canDeleteSelection &&
                ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
                !state.MultiSelectedAssetKeys.empty())
            {
                if (DeleteAssetKeysWithSceneHandling(assetsDirectory, state.MultiSelectedAssetKeys, onDeleteSceneAssetsRequested))
                    clearProjectAssetSelection();
            }
        }

        if (!state.PendingExternalDropPaths.empty() &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            const std::filesystem::path targetFolder = state.HoveredFolderRelativePathForExternalDrop;
            const bool importedAny = ProjectAssetOperations::ImportExternalPathsToFolder(
                state.PendingExternalDropPaths, targetFolder);
            if (importedAny)
            {
                InvalidateProjectDirectoryCache();
                LT_INFO("Imported {} external path(s) into Assets/{}",
                        state.PendingExternalDropPaths.size(),
                        targetFolder.generic_string());
            }
            state.PendingExternalDropPaths.clear();
        }

        DrawProjectFolderPopups(
            assetsDirectory,
            state,
            onCreateMaterialRequested,
            onCreateTilesetRequested,
            onCreateAudioMixerRequested,
            onCreateInputActionsRequested,
            onCreateAnimationClipRequested,
            onCreateAnimatorControllerRequested,
            onAssetRenamed);
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(5);
        gProjectSearchMatchCache.clear();
        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}
