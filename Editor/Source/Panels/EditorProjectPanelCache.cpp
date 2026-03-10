#include "EditorProjectPanelInternal.h"
#include "EditorProjectPanelShared.h"

#include "EditorAssetNaming.h"
#include "EditorAssetPreview.h"
#include "Assets/AssetPaths.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Debug/Log.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/NativeRenderHandles.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Limitless::EditorProjectPanel::Internal
{
    namespace
    {
        ProjectPanelCacheState& GetProjectPanelCacheState(EditorProjectPanelState& state)
        {
            if (!state.CacheState)
                state.CacheState = std::make_shared<ProjectPanelCacheState>();
            return *state.CacheState;
        }

        std::string NormalizeLooseAssetKey(std::string assetKey)
        {
            for (char& character : assetKey)
            {
                if (character == '\\')
                    character = '/';
            }

            auto trim = [](std::string_view value) -> std::string {
                size_t begin = 0;
                size_t end = value.size();
                while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
                    ++begin;
                while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
                    --end;
                return std::string(value.substr(begin, end - begin));
            };

            std::string collapsed;
            collapsed.reserve(assetKey.size());
            bool previousWasSeparator = false;
            for (const char character : assetKey)
            {
                if (character == '/')
                {
                    if (!previousWasSeparator)
                        collapsed.push_back('/');
                    previousWasSeparator = true;
                }
                else
                {
                    collapsed.push_back(character);
                    previousWasSeparator = false;
                }
            }

            std::string rebuilt;
            size_t segmentStart = 0;
            while (segmentStart < collapsed.size())
            {
                const size_t separator = collapsed.find('/', segmentStart);
                const bool hasSeparator = separator != std::string::npos;
                const size_t segmentEnd = hasSeparator ? separator : collapsed.size();
                const std::string trimmedSegment = trim(std::string_view(collapsed).substr(segmentStart, segmentEnd - segmentStart));
                rebuilt += trimmedSegment;
                if (hasSeparator)
                {
                    rebuilt.push_back('/');
                    segmentStart = separator + 1;
                }
                else
                {
                    break;
                }
            }

            return rebuilt;
        }

        Result<std::unique_ptr<Scene>> LoadPrefabSceneForThumbnail(const std::string& prefabAssetKey)
        {
            auto loadedSceneResult = Scene::LoadFromFile(prefabAssetKey);
            if (!loadedSceneResult.IsFailure())
                return loadedSceneResult;

            if (const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(prefabAssetKey); resolvedPathResult.IsSuccess())
            {
                auto resolvedLoadResult = Scene::LoadFromFile(resolvedPathResult.GetValue());
                if (!resolvedLoadResult.IsFailure())
                    return resolvedLoadResult;
            }

            const std::string normalizedKey = NormalizeLooseAssetKey(prefabAssetKey);
            if (!normalizedKey.empty() && normalizedKey != prefabAssetKey)
            {
                auto normalizedLoadResult = Scene::LoadFromFile(normalizedKey);
                if (!normalizedLoadResult.IsFailure())
                    return normalizedLoadResult;

                if (const auto normalizedResolvedPathResult = Assets::ResolveAssetKeyToPath(normalizedKey); normalizedResolvedPathResult.IsSuccess())
                {
                    auto normalizedResolvedLoadResult = Scene::LoadFromFile(normalizedResolvedPathResult.GetValue());
                    if (!normalizedResolvedLoadResult.IsFailure())
                        return normalizedResolvedLoadResult;
                }
            }

            return loadedSceneResult;
        }

        void PreloadPrefabThumbnailSceneAssets(EditorProjectPanelState& state, Scene& prefabScene)
        {
            auto& registry = prefabScene.GetRegistry();

            auto spriteView = registry.view<SpriteComponent>();
            for (entt::entity entity : spriteView)
            {
                const auto& sprite = spriteView.get<SpriteComponent>(entity);
                if (!sprite.TextureKey.empty())
                    (void)GetCachedThumbnailTextureAsset(state, sprite.TextureKey);
            }

            auto materialView = registry.view<MaterialComponent>();
            for (entt::entity entity : materialView)
            {
                const auto& material = materialView.get<MaterialComponent>(entity);
                if (!material.MaterialKey.empty())
                    (void)Assets::MaterialAsset::LoadBlocking(material.MaterialKey);
            }
        }

        void ConfigurePrefabThumbnailCamera(const Scene& prefabScene, PerspectiveCamera3D& previewCamera)
        {
            if (const auto& bookmark = prefabScene.GetEditorCameraBookmark(); bookmark.has_value())
            {
                previewCamera.SetPosition(bookmark->Position);
                previewCamera.SetYawPitchDegrees(bookmark->YawDegrees, bookmark->PitchDegrees);
            }
            else
            {
                previewCamera.SetPosition(glm::vec3(0.0f, 0.0f, 5.0f));
                previewCamera.SetYawPitchDegrees(-90.0f, 0.0f);
            }
        }

        bool TryPopulatePrefabSnapshotThumbnail(EditorProjectPanelState& state, Scene& prefabScene, PrefabThumbnailCacheEntry& entry)
        {
            PreloadPrefabThumbnailSceneAssets(state, prefabScene);

            FramebufferSpecification specification{};
            specification.Width = kPrefabThumbnailSnapshotSize;
            specification.Height = kPrefabThumbnailSnapshotSize;
            specification.Samples = 1;
            specification.DepthAttachment = true;
            specification.StencilAttachment = false;

            std::shared_ptr<Framebuffer> framebuffer = Framebuffer::Create(specification);
            if (!framebuffer)
                return false;

            PerspectiveCamera3D previewCamera(
                CameraId{ 1 },
                "PrefabThumbnailPreview",
                CameraUsage::Editor,
                kPrefabThumbnailSnapshotSize,
                kPrefabThumbnailSnapshotSize);
            ConfigurePrefabThumbnailCamera(prefabScene, previewCamera);
            SceneRenderer::RenderToViewport(
                prefabScene,
                previewCamera,
                framebuffer,
                kPrefabThumbnailSnapshotSize,
                kPrefabThumbnailSnapshotSize);

            std::shared_ptr<Texture2D> previewTexture = framebuffer->GetColorAttachment();
            if (!previewTexture)
                return false;

            entry.PreviewTexture = previewTexture;
            entry.UvMin = ImVec2(0.0f, 1.0f);
            entry.UvMax = ImVec2(1.0f, 0.0f);
            entry.SourceWidth = static_cast<float>(std::max(1u, previewTexture->GetWidth()));
            entry.SourceHeight = static_cast<float>(std::max(1u, previewTexture->GetHeight()));
            entry.HasPreview = true;
            return true;
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

    }

    void ClearProjectPanelCaches(EditorProjectPanelState& state)
    {
        state.CacheState.reset();
    }

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

    const AssetTypeBadgeInfo& ResolveAssetBadge(bool isTexture,
                                                bool isScene,
                                                bool isMaterial,
                                                bool isAudioMixer,
                                                bool isInputActions,
                                                bool isAnimationClip,
                                                bool isAnimatorController,
                                                bool isPrefab,
                                                bool isShader,
                                                bool isAudio,
                                                bool isFont,
                                                bool isNativeScriptFile,
                                                bool isManagedScriptFile)
    {
        if (isTexture) return kBadgeTexture;
        if (isScene) return kBadgeScene;
        if (isMaterial) return kBadgeMaterial;
        if (isAudioMixer) return kBadgeAudioMixer;
        if (isInputActions) return kBadgeInputActions;
        if (isAnimationClip) return kBadgeAnimationClip;
        if (isAnimatorController) return kBadgeAnimController;
        if (isPrefab) return kBadgePrefab;
        if (isShader) return kBadgeShader;
        if (isAudio) return kBadgeAudio;
        if (isFont) return kBadgeFont;
        if (isManagedScriptFile) return kBadgeManagedScript;
        if (isNativeScriptFile) return kBadgeScript;
        return kBadgeUnknown;
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

    bool IsManagedScriptExtensionLower(const std::string& lowerExtension)
    {
        return lowerExtension == ".cs";
    }

    bool IsScriptAssetExtensionLower(const std::string& lowerExtension)
    {
        return IsNativeScriptExtensionLower(lowerExtension) || IsManagedScriptExtensionLower(lowerExtension);
    }

    const Assets::SpriteImportSettings& GetCachedSpriteImportSettings(EditorProjectPanelState& state, const std::string& textureAssetKey)
    {
        ProjectPanelCacheState& cacheState = GetProjectPanelCacheState(state);
        auto it = cacheState.SpriteSettingsCache.find(textureAssetKey);
        const auto now = std::chrono::steady_clock::now();

        if (it != cacheState.SpriteSettingsCache.end() &&
            (now - it->second.LoadTime) < kSpriteSettingsCacheLifetime)
        {
            return it->second.Settings;
        }

        SpriteSettingsCacheEntry entry;
        entry.Settings = Assets::LoadSpriteImportSettings(textureAssetKey);
        entry.LoadTime = now;
        auto [insertedIt, _] = cacheState.SpriteSettingsCache.insert_or_assign(textureAssetKey, std::move(entry));
        return insertedIt->second.Settings;
    }

    Assets::TextureAsset::Ptr GetCachedThumbnailTextureAsset(EditorProjectPanelState& state, const std::string& textureAssetKey)
    {
        if (textureAssetKey.empty())
            return nullptr;

        ProjectPanelCacheState& cacheState = GetProjectPanelCacheState(state);
        const auto now = std::chrono::steady_clock::now();
        if (auto it = cacheState.TextureThumbnailCache.find(textureAssetKey); it != cacheState.TextureThumbnailCache.end())
        {
            if ((now - it->second.LoadTime) < kTextureThumbnailCacheLifetime)
                return it->second.TextureAsset;
        }

        TextureThumbnailCacheEntry entry;
        entry.TextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(textureAssetKey);
        entry.LoadTime = now;
        auto [insertedIt, _] = cacheState.TextureThumbnailCache.insert_or_assign(textureAssetKey, std::move(entry));
        return insertedIt->second.TextureAsset;
    }

    const PrefabThumbnailCacheEntry* GetCachedPrefabThumbnail(EditorProjectPanelState& state, const std::string& prefabAssetKey)
    {
        if (prefabAssetKey.empty())
            return nullptr;

        ProjectPanelCacheState& cacheState = GetProjectPanelCacheState(state);
        const auto now = std::chrono::steady_clock::now();
        if (auto it = cacheState.PrefabThumbnailCache.find(prefabAssetKey); it != cacheState.PrefabThumbnailCache.end())
        {
            if ((now - it->second.LoadTime) < kPrefabThumbnailCacheLifetime)
                return it->second.HasPreview ? &it->second : nullptr;
        }

        PrefabThumbnailCacheEntry entry;
        entry.LoadTime = now;

        const auto loadedSceneResult = LoadPrefabSceneForThumbnail(prefabAssetKey);
        if (loadedSceneResult.IsSuccess() && loadedSceneResult.GetValue())
        {
            Scene& prefabScene = *loadedSceneResult.GetValue();
            (void)TryPopulatePrefabSnapshotThumbnail(state, prefabScene, entry);
        }

        auto [insertedIt, _] = cacheState.PrefabThumbnailCache.insert_or_assign(prefabAssetKey, std::move(entry));
        return insertedIt->second.HasPreview ? &insertedIt->second : nullptr;
    }

    const std::vector<ProjectAssetTreeEntry>& GetCachedProjectDirectoryEntries(EditorProjectPanelState& state,
                                                                                const std::filesystem::path& assetsDirectory,
                                                                                const std::filesystem::path& relativePath)
    {
        ProjectPanelCacheState& cacheState = GetProjectPanelCacheState(state);
        const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
        const std::string cacheKey = currentDirectory.lexically_normal().generic_string();
        ProjectAssetDirectoryCacheEntry& cacheEntry = cacheState.ProjectAssetDirectoryCache[cacheKey];

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

    std::string BuildProjectScriptAssetDisplayName(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (extension == ".cs")
            return path.stem().string() + " [C# Script]";
        if (extension == ".h")
            return path.stem().string() + " [.h Header]";
        return path.stem().string() + " [.cpp Source]";
    }

    std::string GetAssetDisplayName(const std::filesystem::path& path)
    {
        return EditorAssetNaming::GetAssetDisplayNameFromPath(path);
    }

    void DrawProjectGridEntryPreview(EditorProjectPanelState& state,
                                     EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                     const ProjectGridEntry& entry,
                                     const ImVec2& previewMin,
                                     const ImVec2& previewMax)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const AssetTypeBadgeInfo& badge = *entry.Badge;

        const auto drawThumbnailImage = [&](const std::shared_ptr<Texture>& textureHandle,
                                            const float sourceWidth,
                                            const float sourceHeight,
                                            const ImVec2& uvMin,
                                            const ImVec2& uvMax) -> bool {
            if (!textureHandle)
                return false;

            const float imageInset = 4.0f;
            const float previewWidth = std::max(1.0f, (previewMax.x - previewMin.x) - imageInset * 2.0f);
            const float previewInnerHeight = std::max(1.0f, (previewMax.y - previewMin.y) - imageInset * 2.0f);
            const float clampedSourceWidth = std::max(1.0f, sourceWidth);
            const float clampedSourceHeight = std::max(1.0f, sourceHeight);
            const float scale = std::min(previewWidth / clampedSourceWidth, previewInnerHeight / clampedSourceHeight);
            const float drawWidth = std::max(1.0f, clampedSourceWidth * scale);
            const float drawHeight = std::max(1.0f, clampedSourceHeight * scale);
            const ImVec2 imageMin(
                previewMin.x + imageInset + (previewWidth - drawWidth) * 0.5f,
                previewMin.y + imageInset + (previewInnerHeight - drawHeight) * 0.5f);
            const ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);
            drawList->AddImage(
                static_cast<ImTextureID>(GetTextureNativeHandle(textureHandle)),
                imageMin,
                imageMax,
                uvMin,
                uvMax,
                IM_COL32_WHITE);
            return true;
        };

        const auto drawFolderThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float iconWidth = std::max(20.0f, std::min(previewWidth * 0.66f, previewWidth - 18.0f));
            const float iconHeight = std::max(16.0f, std::min(previewHeightInner * 0.46f, previewHeightInner - 20.0f));
            const float tabWidth = std::max(12.0f, iconWidth * 0.36f);
            const float tabHeight = std::max(6.0f, iconHeight * 0.28f);
            const float bodyTopOffset = tabHeight * 0.72f;

            const ImVec2 bodyMin(
                previewMin.x + (previewWidth - iconWidth) * 0.5f,
                previewMin.y + (previewHeightInner - (iconHeight + bodyTopOffset)) * 0.5f + bodyTopOffset);
            const ImVec2 bodyMax(bodyMin.x + iconWidth, bodyMin.y + iconHeight);
            const ImVec2 tabMin(bodyMin.x + iconWidth * 0.08f, bodyMin.y - bodyTopOffset);
            const ImVec2 tabMax(tabMin.x + tabWidth, tabMin.y + tabHeight);

            drawList->AddRectFilled(
                ImVec2(bodyMin.x + 1.0f, bodyMin.y + 2.0f),
                ImVec2(bodyMax.x + 1.0f, bodyMax.y + 2.0f),
                IM_COL32(0, 0, 0, 40),
                6.0f);
            drawList->AddRectFilled(bodyMin, bodyMax, IM_COL32(214, 168, 78, 255), 6.0f);
            drawList->AddRect(bodyMin, bodyMax, IM_COL32(245, 208, 120, 255), 6.0f, 0, 1.0f);
            drawList->AddRectFilled(tabMin, tabMax, IM_COL32(232, 189, 97, 255), 5.0f);
            drawList->AddRect(tabMin, tabMax, IM_COL32(248, 217, 140, 255), 5.0f, 0, 1.0f);

            const ImVec2 accentMin(bodyMin.x + iconWidth * 0.08f, bodyMin.y + iconHeight * 0.22f);
            const ImVec2 accentMax(bodyMax.x - iconWidth * 0.10f, accentMin.y + std::max(2.0f, iconHeight * 0.12f));
            drawList->AddRectFilled(accentMin, accentMax, IM_COL32(246, 223, 168, 90), 3.0f);
        };

        const auto drawSceneThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float frameWidth = std::max(22.0f, std::min(previewWidth * 0.70f, previewWidth - 18.0f));
            const float frameHeight = std::max(18.0f, std::min(previewHeightInner * 0.56f, previewHeightInner - 18.0f));
            const ImVec2 frameMin(
                previewMin.x + (previewWidth - frameWidth) * 0.5f,
                previewMin.y + (previewHeightInner - frameHeight) * 0.5f);
            const ImVec2 frameMax(frameMin.x + frameWidth, frameMin.y + frameHeight);
            const float innerInset = 3.0f;
            const ImVec2 innerMin(frameMin.x + innerInset, frameMin.y + innerInset);
            const ImVec2 innerMax(frameMax.x - innerInset, frameMax.y - innerInset);

            drawList->AddRectFilled(
                ImVec2(frameMin.x + 1.0f, frameMin.y + 2.0f),
                ImVec2(frameMax.x + 1.0f, frameMax.y + 2.0f),
                IM_COL32(0, 0, 0, 36),
                6.0f);
            drawList->AddRectFilled(frameMin, frameMax, IM_COL32(67, 96, 132, 255), 6.0f);
            drawList->AddRect(frameMin, frameMax, IM_COL32(129, 169, 220, 255), 6.0f, 0, 1.0f);
            drawList->AddRectFilledMultiColor(
                innerMin,
                innerMax,
                IM_COL32(94, 148, 214, 255),
                IM_COL32(94, 148, 214, 255),
                IM_COL32(42, 69, 110, 255),
                IM_COL32(42, 69, 110, 255));

            const ImVec2 sunCenter(innerMax.x - frameWidth * 0.18f, innerMin.y + frameHeight * 0.22f);
            drawList->AddCircleFilled(sunCenter, std::max(2.0f, frameHeight * 0.08f), IM_COL32(255, 220, 136, 255), 16);

            drawList->AddTriangleFilled(
                ImVec2(innerMin.x + frameWidth * 0.12f, innerMax.y - frameHeight * 0.08f),
                ImVec2(innerMin.x + frameWidth * 0.40f, innerMin.y + frameHeight * 0.32f),
                ImVec2(innerMin.x + frameWidth * 0.68f, innerMax.y - frameHeight * 0.08f),
                IM_COL32(98, 185, 132, 255));
            drawList->AddTriangleFilled(
                ImVec2(innerMin.x + frameWidth * 0.38f, innerMax.y - frameHeight * 0.08f),
                ImVec2(innerMin.x + frameWidth * 0.62f, innerMin.y + frameHeight * 0.18f),
                ImVec2(innerMin.x + frameWidth * 0.88f, innerMax.y - frameHeight * 0.08f),
                IM_COL32(66, 142, 102, 255));

            const ImVec2 groundMin(innerMin.x, innerMax.y - std::max(3.0f, frameHeight * 0.16f));
            drawList->AddRectFilled(groundMin, innerMax, IM_COL32(44, 92, 67, 255), 0.0f);
        };

        const auto drawAudioThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float iconHeight = std::max(18.0f, std::min(previewHeightInner * 0.52f, previewHeightInner - 18.0f));
            const float bodyWidth = std::max(8.0f, iconHeight * 0.22f);
            const float bodyHeight = std::max(12.0f, iconHeight * 0.52f);
            const float coneWidth = std::max(10.0f, iconHeight * 0.34f);
            const float totalWidth = bodyWidth + coneWidth + iconHeight * 0.46f;
            const ImVec2 iconMin(
                previewMin.x + (previewWidth - totalWidth) * 0.5f,
                previewMin.y + (previewHeightInner - iconHeight) * 0.5f);

            const ImVec2 bodyMin(iconMin.x, iconMin.y + (iconHeight - bodyHeight) * 0.5f);
            const ImVec2 bodyMax(bodyMin.x + bodyWidth, bodyMin.y + bodyHeight);
            const ImVec2 coneTop(bodyMax.x + coneWidth, iconMin.y + iconHeight * 0.18f);
            const ImVec2 coneMid(bodyMax.x + coneWidth, iconMin.y + iconHeight * 0.82f);
            const ImVec2 coneJoin(bodyMax.x, iconMin.y + iconHeight * 0.50f);

            drawList->AddRectFilled(bodyMin, bodyMax, IM_COL32(178, 134, 219, 255), 3.0f);
            drawList->AddRect(bodyMin, bodyMax, IM_COL32(220, 190, 248, 255), 3.0f, 0, 1.0f);
            drawList->AddTriangleFilled(coneTop, coneMid, coneJoin, IM_COL32(198, 154, 236, 255));
            drawList->AddTriangle(coneTop, coneMid, coneJoin, IM_COL32(228, 200, 250, 255), 1.0f);

            const ImVec2 waveCenter(bodyMax.x + coneWidth + 2.0f, iconMin.y + iconHeight * 0.50f);
            const float waveRadiusA = std::max(4.0f, iconHeight * 0.18f);
            const float waveRadiusB = std::max(6.0f, iconHeight * 0.28f);
            drawList->PathArcTo(waveCenter, waveRadiusA, -0.85f, 0.85f, 18);
            drawList->PathStroke(IM_COL32(214, 181, 245, 255), 0, 2.0f);
            drawList->PathArcTo(waveCenter, waveRadiusB, -0.85f, 0.85f, 18);
            drawList->PathStroke(IM_COL32(174, 130, 224, 255), 0, 2.0f);
        };

        const auto drawInputThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float deviceWidth = std::max(24.0f, std::min(previewWidth * 0.68f, previewWidth - 18.0f));
            const float deviceHeight = std::max(16.0f, std::min(previewHeightInner * 0.40f, previewHeightInner - 20.0f));
            const ImVec2 deviceMin(
                previewMin.x + (previewWidth - deviceWidth) * 0.5f,
                previewMin.y + (previewHeightInner - deviceHeight) * 0.5f);
            const ImVec2 deviceMax(deviceMin.x + deviceWidth, deviceMin.y + deviceHeight);

            drawList->AddRectFilled(
                ImVec2(deviceMin.x + 1.0f, deviceMin.y + 2.0f),
                ImVec2(deviceMax.x + 1.0f, deviceMax.y + 2.0f),
                IM_COL32(0, 0, 0, 34),
                5.0f);
            drawList->AddRectFilled(deviceMin, deviceMax, IM_COL32(82, 148, 88, 255), 5.0f);
            drawList->AddRect(deviceMin, deviceMax, IM_COL32(145, 214, 151, 255), 5.0f, 0, 1.0f);

            const ImVec2 dpadCenter(deviceMin.x + deviceWidth * 0.24f, deviceMin.y + deviceHeight * 0.52f);
            const float dpadArm = std::max(2.0f, deviceHeight * 0.12f);
            const float dpadLength = std::max(4.0f, deviceHeight * 0.24f);
            drawList->AddRectFilled(ImVec2(dpadCenter.x - dpadArm, dpadCenter.y - dpadLength), ImVec2(dpadCenter.x + dpadArm, dpadCenter.y + dpadLength), IM_COL32(219, 244, 221, 220), 1.5f);
            drawList->AddRectFilled(ImVec2(dpadCenter.x - dpadLength, dpadCenter.y - dpadArm), ImVec2(dpadCenter.x + dpadLength, dpadCenter.y + dpadArm), IM_COL32(219, 244, 221, 220), 1.5f);

            const ImVec2 buttonA(deviceMin.x + deviceWidth * 0.72f, deviceMin.y + deviceHeight * 0.42f);
            const ImVec2 buttonB(deviceMin.x + deviceWidth * 0.82f, deviceMin.y + deviceHeight * 0.58f);
            drawList->AddCircleFilled(buttonA, std::max(2.0f, deviceHeight * 0.10f), IM_COL32(220, 245, 223, 230), 16);
            drawList->AddCircleFilled(buttonB, std::max(2.0f, deviceHeight * 0.10f), IM_COL32(198, 233, 202, 210), 16);

            const ImVec2 cableStart(deviceMin.x + deviceWidth * 0.44f, deviceMin.y);
            const ImVec2 cablePeak(deviceMin.x + deviceWidth * 0.54f, deviceMin.y - std::max(5.0f, deviceHeight * 0.30f));
            const ImVec2 cableEnd(deviceMin.x + deviceWidth * 0.64f, deviceMin.y + 2.0f);
            drawList->AddBezierCubic(cableStart, cablePeak, cablePeak, cableEnd, IM_COL32(186, 235, 190, 180), 1.5f, 20);
        };

        const auto drawAnimationClipThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float stripWidth = std::max(24.0f, std::min(previewWidth * 0.72f, previewWidth - 18.0f));
            const float stripHeight = std::max(16.0f, std::min(previewHeightInner * 0.46f, previewHeightInner - 20.0f));
            const ImVec2 stripMin(
                previewMin.x + (previewWidth - stripWidth) * 0.5f,
                previewMin.y + (previewHeightInner - stripHeight) * 0.5f);
            const ImVec2 stripMax(stripMin.x + stripWidth, stripMin.y + stripHeight);

            drawList->AddRectFilled(
                ImVec2(stripMin.x + 1.0f, stripMin.y + 2.0f),
                ImVec2(stripMax.x + 1.0f, stripMax.y + 2.0f),
                IM_COL32(0, 0, 0, 34),
                5.0f);
            drawList->AddRectFilled(stripMin, stripMax, IM_COL32(201, 90, 84, 255), 5.0f);
            drawList->AddRect(stripMin, stripMax, IM_COL32(244, 145, 138, 255), 5.0f, 0, 1.0f);

            const float perforationWidth = std::max(2.0f, stripWidth * 0.06f);
            const float perforationStep = std::max(5.0f, stripHeight * 0.24f);
            const float perforationStartY = stripMin.y + 3.0f;
            const float perforationEndY = stripMax.y - 3.0f;
            if (perforationStartY < perforationEndY)
            {
                const int perforationCount = std::max(0, static_cast<int>((perforationEndY - perforationStartY) / perforationStep));
                for (int perforationIndex = 0; perforationIndex <= perforationCount; ++perforationIndex)
                {
                    const float y = perforationStartY + static_cast<float>(perforationIndex) * perforationStep;
                    if (y >= perforationEndY)
                        break;
                    drawList->AddRectFilled(ImVec2(stripMin.x + 2.0f, y), ImVec2(stripMin.x + 2.0f + perforationWidth, std::min(stripMax.y - 2.0f, y + 2.0f)), IM_COL32(255, 220, 216, 190), 1.0f);
                    drawList->AddRectFilled(ImVec2(stripMax.x - 2.0f - perforationWidth, y), ImVec2(stripMax.x - 2.0f, std::min(stripMax.y - 2.0f, y + 2.0f)), IM_COL32(255, 220, 216, 190), 1.0f);
                }
            }

            const ImVec2 playA(stripMin.x + stripWidth * 0.40f, stripMin.y + stripHeight * 0.26f);
            const ImVec2 playB(stripMin.x + stripWidth * 0.40f, stripMax.y - stripHeight * 0.26f);
            const ImVec2 playC(stripMax.x - stripWidth * 0.28f, stripMin.y + stripHeight * 0.50f);
            drawList->AddTriangleFilled(playA, playB, playC, IM_COL32(255, 232, 228, 230));
        };

        const auto drawAnimatorControllerThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float nodeRadius = std::max(3.0f, std::min(previewHeightInner * 0.10f, 6.0f));
            const ImVec2 leftNode(previewMin.x + previewWidth * 0.26f, previewMin.y + previewHeightInner * 0.52f);
            const ImVec2 topRightNode(previewMin.x + previewWidth * 0.66f, previewMin.y + previewHeightInner * 0.30f);
            const ImVec2 bottomRightNode(previewMin.x + previewWidth * 0.72f, previewMin.y + previewHeightInner * 0.68f);

            drawList->AddBezierCubic(
                leftNode,
                ImVec2(previewMin.x + previewWidth * 0.42f, leftNode.y - previewHeightInner * 0.14f),
                ImVec2(previewMin.x + previewWidth * 0.52f, topRightNode.y + previewHeightInner * 0.04f),
                topRightNode,
                IM_COL32(238, 134, 158, 220),
                2.0f,
                18);
            drawList->AddBezierCubic(
                leftNode,
                ImVec2(previewMin.x + previewWidth * 0.44f, leftNode.y + previewHeightInner * 0.12f),
                ImVec2(previewMin.x + previewWidth * 0.54f, bottomRightNode.y - previewHeightInner * 0.06f),
                bottomRightNode,
                IM_COL32(214, 106, 136, 210),
                2.0f,
                18);

            drawList->AddCircleFilled(leftNode, nodeRadius, IM_COL32(255, 196, 211, 255), 16);
            drawList->AddCircleFilled(topRightNode, nodeRadius, IM_COL32(255, 154, 182, 255), 16);
            drawList->AddCircleFilled(bottomRightNode, nodeRadius, IM_COL32(233, 110, 145, 255), 16);
            drawList->AddCircle(leftNode, nodeRadius + 1.0f, IM_COL32(255, 225, 231, 180), 16, 1.0f);
            drawList->AddCircle(topRightNode, nodeRadius + 1.0f, IM_COL32(255, 209, 220, 180), 16, 1.0f);
            drawList->AddCircle(bottomRightNode, nodeRadius + 1.0f, IM_COL32(255, 196, 211, 170), 16, 1.0f);
        };

        const auto drawScriptDocumentThumbnail = [&](const ImU32 bodyColor,
                                                     const ImU32 borderColorLocal,
                                                     const ImU32 accentColor,
                                                     const char* label) {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float docWidth = std::max(18.0f, std::min(previewWidth * 0.42f, previewWidth - 24.0f));
            const float docHeight = std::max(22.0f, std::min(previewHeightInner * 0.62f, previewHeightInner - 16.0f));
            const float foldSize = std::max(5.0f, std::min(docWidth * 0.24f, docHeight * 0.22f));
            const ImVec2 docMin(
                previewMin.x + (previewWidth - docWidth) * 0.5f,
                previewMin.y + (previewHeightInner - docHeight) * 0.5f);
            const ImVec2 docMax(docMin.x + docWidth, docMin.y + docHeight);
            const ImVec2 foldA(docMax.x - foldSize, docMin.y);
            const ImVec2 foldB(docMax.x, docMin.y + foldSize);
            const ImVec2 foldC(docMax.x - foldSize, docMin.y + foldSize);

            drawList->AddRectFilled(
                ImVec2(docMin.x + 1.0f, docMin.y + 2.0f),
                ImVec2(docMax.x + 1.0f, docMax.y + 2.0f),
                IM_COL32(0, 0, 0, 34),
                5.0f);
            drawList->AddRectFilled(docMin, docMax, bodyColor, 5.0f);
            drawList->AddRect(docMin, docMax, borderColorLocal, 5.0f, 0, 1.0f);
            drawList->AddTriangleFilled(foldA, ImVec2(docMax.x, docMin.y), foldB, accentColor);
            drawList->AddLine(foldA, foldB, borderColorLocal, 1.0f);
            drawList->AddLine(foldA, foldC, borderColorLocal, 1.0f);

            const float lineInsetX = std::max(4.0f, docWidth * 0.16f);
            const float lineStartY = docMin.y + docHeight * 0.24f;
            const float lineStep = std::max(3.0f, docHeight * 0.13f);
            for (int32_t lineIndex = 0; lineIndex < 3; ++lineIndex)
            {
                const float lineY = lineStartY + static_cast<float>(lineIndex) * lineStep;
                drawList->AddLine(
                    ImVec2(docMin.x + lineInsetX, lineY),
                    ImVec2(docMax.x - lineInsetX - (lineIndex == 2 ? docWidth * 0.12f : 0.0f), lineY),
                    accentColor,
                    1.0f);
            }

            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            drawList->AddText(
                ImVec2(docMin.x + (docWidth - labelSize.x) * 0.5f,
                       docMax.y - docHeight * 0.28f - labelSize.y * 0.5f),
                borderColorLocal,
                label);
        };

        const auto drawManagedScriptThumbnail = [&]() {
            drawScriptDocumentThumbnail(
                IM_COL32(74, 120, 64, 255),
                IM_COL32(186, 232, 177, 255),
                IM_COL32(214, 244, 208, 210),
                "C#");
        };

        const auto drawNativeScriptThumbnail = [&]() {
            drawScriptDocumentThumbnail(
                IM_COL32(148, 128, 44, 255),
                IM_COL32(241, 224, 132, 255),
                IM_COL32(255, 242, 188, 210),
                "C++");
        };

        const auto drawTilesetThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float boardSize = std::max(20.0f, std::min(std::min(previewWidth, previewHeightInner) * 0.62f, std::min(previewWidth, previewHeightInner) - 16.0f));
            const ImVec2 boardMin(
                previewMin.x + (previewWidth - boardSize) * 0.5f,
                previewMin.y + (previewHeightInner - boardSize) * 0.5f);
            const ImVec2 boardMax(boardMin.x + boardSize, boardMin.y + boardSize);
            const float cellSize = boardSize / 3.0f;
            constexpr ImU32 tileColors[9] = {
                IM_COL32(182, 140, 86, 255), IM_COL32(92, 156, 104, 255), IM_COL32(78, 132, 198, 255),
                IM_COL32(206, 96, 92, 255), IM_COL32(218, 178, 92, 255), IM_COL32(126, 108, 198, 255),
                IM_COL32(88, 168, 170, 255), IM_COL32(176, 118, 84, 255), IM_COL32(116, 184, 124, 255)
            };

            drawList->AddRectFilled(
                ImVec2(boardMin.x + 1.0f, boardMin.y + 2.0f),
                ImVec2(boardMax.x + 1.0f, boardMax.y + 2.0f),
                IM_COL32(0, 0, 0, 34),
                5.0f);
            for (int32_t row = 0; row < 3; ++row)
            {
                for (int32_t column = 0; column < 3; ++column)
                {
                    const int32_t colorIndex = row * 3 + column;
                    const ImVec2 cellMin(boardMin.x + cellSize * static_cast<float>(column), boardMin.y + cellSize * static_cast<float>(row));
                    const ImVec2 cellMax(cellMin.x + cellSize - 1.0f, cellMin.y + cellSize - 1.0f);
                    drawList->AddRectFilled(cellMin, cellMax, tileColors[colorIndex], 2.0f);
                }
            }
            drawList->AddRect(boardMin, boardMax, IM_COL32(234, 216, 180, 200), 5.0f, 0, 1.0f);
        };

        const auto drawTilePaletteThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float paletteWidth = std::max(24.0f, std::min(previewWidth * 0.68f, previewWidth - 16.0f));
            const float paletteHeight = std::max(18.0f, std::min(previewHeightInner * 0.52f, previewHeightInner - 18.0f));
            const ImVec2 paletteMin(
                previewMin.x + (previewWidth - paletteWidth) * 0.5f,
                previewMin.y + (previewHeightInner - paletteHeight) * 0.5f);
            const ImVec2 paletteMax(paletteMin.x + paletteWidth, paletteMin.y + paletteHeight);
            const float swatchWidth = paletteWidth / 4.0f;
            constexpr ImU32 swatchColors[4] = {
                IM_COL32(234, 109, 103, 255),
                IM_COL32(242, 191, 92, 255),
                IM_COL32(104, 186, 122, 255),
                IM_COL32(92, 156, 230, 255)
            };

            drawList->AddRectFilled(
                ImVec2(paletteMin.x + 1.0f, paletteMin.y + 2.0f),
                ImVec2(paletteMax.x + 1.0f, paletteMax.y + 2.0f),
                IM_COL32(0, 0, 0, 34),
                6.0f);
            drawList->AddRectFilled(paletteMin, paletteMax, IM_COL32(88, 74, 52, 255), 6.0f);
            for (int32_t swatchIndex = 0; swatchIndex < 4; ++swatchIndex)
            {
                const ImVec2 swatchMin(paletteMin.x + swatchWidth * static_cast<float>(swatchIndex), paletteMin.y);
                const ImVec2 swatchMax(swatchMin.x + swatchWidth, paletteMax.y - paletteHeight * 0.22f);
                drawList->AddRectFilled(swatchMin, swatchMax, swatchColors[swatchIndex], swatchIndex == 0 || swatchIndex == 3 ? 6.0f : 0.0f);
            }
            drawList->AddRect(paletteMin, paletteMax, IM_COL32(236, 220, 193, 190), 6.0f, 0, 1.0f);
            drawList->AddLine(
                ImVec2(paletteMin.x + paletteWidth * 0.18f, paletteMax.y - paletteHeight * 0.18f),
                ImVec2(paletteMax.x - paletteWidth * 0.18f, paletteMax.y - paletteHeight * 0.18f),
                IM_COL32(249, 236, 214, 180),
                2.0f);
        };

        const auto drawAudioMixerThumbnail = [&]() {
            const float previewWidth = previewMax.x - previewMin.x;
            const float previewHeightInner = previewMax.y - previewMin.y;
            const float sliderHeight = std::max(20.0f, std::min(previewHeightInner * 0.56f, previewHeightInner - 16.0f));
            const float sliderTop = previewMin.y + (previewHeightInner - sliderHeight) * 0.5f;
            const float trackSpacing = previewWidth * 0.20f;
            const float startX = previewMin.x + previewWidth * 0.30f;
            const float trackBottom = sliderTop + sliderHeight;
            const float knobRadius = std::max(3.0f, sliderHeight * 0.08f);
            const float knobOffsets[3] = { 0.28f, 0.56f, 0.40f };

            for (int32_t trackIndex = 0; trackIndex < 3; ++trackIndex)
            {
                const float trackX = startX + trackSpacing * static_cast<float>(trackIndex);
                drawList->AddLine(
                    ImVec2(trackX, sliderTop),
                    ImVec2(trackX, trackBottom),
                    IM_COL32(194, 152, 224, 210),
                    2.0f);
                const float knobY = sliderTop + sliderHeight * knobOffsets[trackIndex];
                drawList->AddCircleFilled(ImVec2(trackX, knobY), knobRadius + 1.0f, IM_COL32(106, 69, 138, 255), 16);
                drawList->AddCircleFilled(ImVec2(trackX, knobY), knobRadius, IM_COL32(234, 204, 255, 255), 16);
            }
        };

        bool drewPreviewImage = false;
        if (entry.IsTexture)
        {
            if (Assets::TextureAsset::Ptr textureAsset = GetCachedThumbnailTextureAsset(state, entry.PrimaryAssetKey))
            {
                if (const auto& textureHandle = textureAsset->GetTexture(); textureHandle)
                {
                    float textureWidth = static_cast<float>(std::max(1u, textureHandle->GetWidth()));
                    float textureHeight = static_cast<float>(std::max(1u, textureHandle->GetHeight()));
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
                        textureWidth = static_cast<float>(std::max(1, entry.ThumbnailRectPixels.z));
                        textureHeight = static_cast<float>(std::max(1, entry.ThumbnailRectPixels.w));
                    }
                    drewPreviewImage = drawThumbnailImage(textureHandle, textureWidth, textureHeight, uvMin, uvMax);
                }
            }
        }
        else if (entry.IsMaterial)
        {
            if (const EditorAssetPreview::MaterialPreviewData* materialPreview = EditorAssetPreview::GetCachedMaterialPreview(materialPreviewCache, entry.PrimaryAssetKey))
            {
                drewPreviewImage = drawThumbnailImage(
                    materialPreview->PreviewTexture,
                    materialPreview->SourceWidth,
                    materialPreview->SourceHeight,
                    materialPreview->UvMin,
                    materialPreview->UvMax);
            }
        }
        else if (entry.IsPrefab)
        {
            if (const PrefabThumbnailCacheEntry* prefabThumbnail = GetCachedPrefabThumbnail(state, entry.PrimaryAssetKey))
            {
                drewPreviewImage = drawThumbnailImage(
                    prefabThumbnail->PreviewTexture,
                    prefabThumbnail->SourceWidth,
                    prefabThumbnail->SourceHeight,
                    prefabThumbnail->UvMin,
                    prefabThumbnail->UvMax);
            }
        }

        if (!drewPreviewImage && entry.IsDirectory)
            drawFolderThumbnail();
        else if (!drewPreviewImage && entry.IsScene)
            drawSceneThumbnail();
        else if (!drewPreviewImage && entry.IsAudio)
            drawAudioThumbnail();
        else if (!drewPreviewImage && entry.IsInputActions)
            drawInputThumbnail();
        else if (!drewPreviewImage && entry.IsAnimationClip)
            drawAnimationClipThumbnail();
        else if (!drewPreviewImage && entry.IsAnimatorController)
            drawAnimatorControllerThumbnail();
        else if (!drewPreviewImage && entry.IsTileset)
            drawTilesetThumbnail();
        else if (!drewPreviewImage && entry.IsTilePalette)
            drawTilePaletteThumbnail();
        else if (!drewPreviewImage && entry.IsAudioMixer)
            drawAudioMixerThumbnail();
        else if (!drewPreviewImage && entry.IsManagedScriptFile)
            drawManagedScriptThumbnail();
        else if (!drewPreviewImage && entry.IsNativeScriptFile)
            drawNativeScriptThumbnail();
        else if (!drewPreviewImage)
        {
            const ImVec2 fallbackTextSize = ImGui::CalcTextSize(badge.Label);
            drawList->AddText(
                ImVec2(previewMin.x + (previewMax.x - previewMin.x - fallbackTextSize.x) * 0.5f,
                       previewMin.y + (previewMax.y - previewMin.y - fallbackTextSize.y) * 0.5f),
                badge.TextColor,
                badge.Label);
        }
    }
}

namespace Limitless::EditorProjectPanel
{
    void InvalidateProjectDirectoryCache(EditorProjectPanelState& state)
    {
        Internal::ClearProjectPanelCaches(state);
        Internal::ClearProjectSearchMatchCache(state);
    }
}
