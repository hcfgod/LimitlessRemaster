#include "EditorInspectorPanelAssetInspectorsShared.h"

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawTextureInspectorInternal(Scene* scene,
                                      std::string& selectedTextureAssetKey,
                                      Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        std::string resolvedTextureAssetKey = selectedTextureAssetKey;
        int32_t selectedSubSpriteIndex = -1;
        std::string parsedTextureAssetKey;
        if (Assets::TryParseSubSpriteAssetKey(selectedTextureAssetKey, parsedTextureAssetKey, selectedSubSpriteIndex))
            resolvedTextureAssetKey = parsedTextureAssetKey;

        if (resolvedTextureAssetKey.empty())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid texture selection.");
            return;
        }

        if (!cachedTextureAsset || cachedTextureAsset->GetKey() != resolvedTextureAssetKey)
            cachedTextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(resolvedTextureAssetKey);

        auto textureAsset = cachedTextureAsset;
        if (!textureAsset)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", resolvedTextureAssetKey.c_str());
            cachedTextureAsset.reset();
            return;
        }

        const auto* texture = textureAsset->GetTexture().get();
        if (!texture)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Texture not ready.");
            return;
        }

        const std::string fileName = std::filesystem::path(resolvedTextureAssetKey).filename().string();
        ImGui::Text("Texture: %s", fileName.c_str());
        ImGui::Text("%u x %u", texture->GetWidth(), texture->GetHeight());
        ImGui::Spacing();

        struct SpriteSettingsCache
        {
            std::string TextureKey;
            Assets::SpriteImportSettings Settings;
            bool Loaded = false;
        };
        static SpriteSettingsCache s_SpriteCache;

        if (!s_SpriteCache.Loaded || s_SpriteCache.TextureKey != resolvedTextureAssetKey)
        {
            s_SpriteCache.TextureKey = resolvedTextureAssetKey;
            s_SpriteCache.Settings = Assets::LoadSpriteImportSettings(resolvedTextureAssetKey);
            s_SpriteCache.Loaded = true;
        }

        auto& spriteSettings = s_SpriteCache.Settings;
        const bool showingSubSpritePreview =
            selectedSubSpriteIndex >= 0 &&
            selectedSubSpriteIndex < static_cast<int32_t>(spriteSettings.SubSprites.size());

        float previewSourceWidth = static_cast<float>(texture->GetWidth());
        float previewSourceHeight = static_cast<float>(texture->GetHeight());
        ImVec2 uv0(0.0f, 1.0f);
        ImVec2 uv1(1.0f, 0.0f);
        if (showingSubSpritePreview)
        {
            const auto& sub = spriteSettings.SubSprites[static_cast<size_t>(selectedSubSpriteIndex)];
            previewSourceWidth = static_cast<float>(std::max(1, sub.RectPixels.z));
            previewSourceHeight = static_cast<float>(std::max(1, sub.RectPixels.w));
            const glm::vec4 subUvs = Assets::ComputeSubSpriteUvs(
                sub.RectPixels,
                texture->GetWidth(),
                texture->GetHeight());
            uv0 = ImVec2(subUvs.x, 1.0f - subUvs.y);
            uv1 = ImVec2(subUvs.z, 1.0f - subUvs.w);

            ImGui::TextDisabled("Sub-Sprite: %s (#%d)",
                                sub.Name.empty() ? "(unnamed)" : sub.Name.c_str(),
                                selectedSubSpriteIndex);
        }
        else if (selectedSubSpriteIndex >= 0)
        {
            ImGui::TextDisabled("Sub-Sprite: invalid index #%d", selectedSubSpriteIndex);
        }

        const float previewSize = 256.0f;
        const float aspect = previewSourceHeight / std::max(1.0f, previewSourceWidth);
        const ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 borderColor(0.4f, 0.4f, 0.4f, 1.0f);
        if (aspect > 1.0f)
        {
            const float width = previewSize / aspect;
            ImGui::Image(static_cast<ImTextureID>(GetTextureNativeHandle(texture)), ImVec2(width, previewSize), uv0, uv1, tintColor, borderColor);
        }
        else
        {
            const float height = previewSize * aspect;
            ImGui::Image(static_cast<ImTextureID>(GetTextureNativeHandle(texture)), ImVec2(previewSize, height), uv0, uv1, tintColor, borderColor);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const char* spriteModeNames[] = { "Single", "Multiple" };
        int spriteModeIndex = static_cast<int>(spriteSettings.Mode);
        if (ImGui::Combo("Sprite Mode", &spriteModeIndex, spriteModeNames, 2))
        {
            spriteSettings.Mode = static_cast<Assets::SpriteImportSettings::SpriteMode>(spriteModeIndex);
            const auto saveResult = Assets::SaveSpriteImportSettings(resolvedTextureAssetKey, spriteSettings);
            if (!saveResult.IsSuccess())
                LT_CORE_WARN("Failed to save sprite import settings for '{}': {}", resolvedTextureAssetKey, saveResult.GetError().GetErrorMessage());
        }

        float ppu = spriteSettings.PixelsPerUnit;
        if (ImGui::DragFloat("Pixels Per Unit", &ppu, 0.5f, 0.01f, 4096.0f, "%.1f"))
        {
            spriteSettings.PixelsPerUnit = std::max(0.01f, ppu);
            const auto saveResult = Assets::SaveSpriteImportSettings(resolvedTextureAssetKey, spriteSettings);
            if (!saveResult.IsSuccess())
                LT_CORE_WARN("Failed to save sprite import settings for '{}': {}", resolvedTextureAssetKey, saveResult.GetError().GetErrorMessage());
        }

        if (spriteSettings.Mode == Assets::SpriteImportSettings::SpriteMode::Multiple)
        {
            ImGui::Spacing();
            if (ImGui::Button("Open Sprite Editor", ImVec2(-1, 0)))
                SetPendingSpriteEditorRequestState(resolvedTextureAssetKey);

            if (!spriteSettings.SubSprites.empty())
            {
                ImGui::TextDisabled("%zu sub-sprites defined", spriteSettings.SubSprites.size());
            }
            else
            {
                ImGui::TextDisabled("No sub-sprites. Open Sprite Editor to slice.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        TextureSpecification specification = textureAsset->GetSpecification();

        const char* filterNames[] = { "Nearest", "Linear" };
        int minimumFilterIndex = static_cast<int>(specification.MinFilter);
        if (ImGui::Combo("Min Filter", &minimumFilterIndex, filterNames, 2))
        {
            specification.MinFilter = static_cast<TextureFilter>(minimumFilterIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        int magnificationFilterIndex = static_cast<int>(specification.MagFilter);
        if (ImGui::Combo("Mag Filter", &magnificationFilterIndex, filterNames, 2))
        {
            specification.MagFilter = static_cast<TextureFilter>(magnificationFilterIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
        int wrapUIndex = static_cast<int>(specification.WrapU);
        if (ImGui::Combo("Wrap U", &wrapUIndex, wrapNames, 2))
        {
            specification.WrapU = static_cast<TextureWrap>(wrapUIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        int wrapVIndex = static_cast<int>(specification.WrapV);
        if (ImGui::Combo("Wrap V", &wrapVIndex, wrapNames, 2))
        {
            specification.WrapV = static_cast<TextureWrap>(wrapVIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Generate Mipmaps");
        bool generateMipmaps = specification.GenerateMipmaps;
        if (ImGui::Checkbox("##GenerateMipmaps", &generateMipmaps))
        {
            specification.GenerateMipmaps = generateMipmaps;
            PersistTextureSpecificationAndReload(scene, textureAsset, specification);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Requires texture reload to take effect.");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Flip Vertically On Load");
        bool flipVerticallyOnLoad = specification.FlipVerticallyOnLoad;
        if (ImGui::Checkbox("##FlipVerticallyOnLoad", &flipVerticallyOnLoad))
        {
            specification.FlipVerticallyOnLoad = flipVerticallyOnLoad;
            PersistTextureSpecificationAndReload(scene, textureAsset, specification);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Requires texture reload to take effect.");
    }

    void DrawTilesetAssetInspectorInternal(Scene* scene, std::string& selectedTilesetAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
            bool PendingSave = false;
        };
        static State s_State;

        if (selectedTilesetAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedTilesetAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedTilesetAssetKey;
            s_State.Loaded = LoadTilesetJson(selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load tileset JSON: %s", selectedTilesetAssetKey.c_str());
                return;
            }
        }

        ImGui::Text("Tileset: %s", std::filesystem::path(selectedTilesetAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedTilesetAssetKey.c_str());
        ImGui::Separator();

        std::string textureKey = s_State.Json.value("TextureKey", std::string{});
        std::string textureLabel = textureKey.empty()
            ? std::string("None")
            : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(textureKey);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Texture");
        ImGui::Button((textureLabel + "##TilesetTexture").c_str(),
                      ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    const std::string resolvedTextureKey = ResolveTextureKeyFromDroppedKey(key);
                    if (!resolvedTextureKey.empty() && textureKey != resolvedTextureKey)
                    {
                        textureKey = resolvedTextureKey;
                        s_State.Json["TextureKey"] = textureKey;
                        (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    const std::string resolvedTextureKey = ResolveTextureKeyFromDroppedKey(key);
                    if (!resolvedTextureKey.empty() && textureKey != resolvedTextureKey)
                    {
                        textureKey = resolvedTextureKey;
                        s_State.Json["TextureKey"] = textureKey;
                        (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("...##TilesetTexturePicker"))
            ImGui::OpenPopup("TilesetTexturePickerPopup");
        if (ImGui::BeginPopup("TilesetTexturePickerPopup"))
        {
            const std::vector<std::string> textureKeys = BuildAssetPickerKeysByType(Assets::AssetType::Texture2D);
            for (const auto& key : textureKeys)
            {
                const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                if (ImGui::Selectable((display + "##TilesetTexturePicker_" + key).c_str(), textureKey == key))
                {
                    textureKey = key;
                    s_State.Json["TextureKey"] = textureKey;
                    (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("X##ClearTilesetTexture"))
        {
            s_State.Json["TextureKey"] = "";
            (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
        }

        std::vector<int> tileSize = s_State.Json.value("TileSizePixels", std::vector<int>{ 16, 16 });
        if (tileSize.size() < 2)
            tileSize = { 16, 16 };
        int32_t tileSizePixels[2] = { std::max(1, tileSize[0]), std::max(1, tileSize[1]) };
        if (ImGui::DragInt2("Tile Size Pixels", tileSizePixels, 1.0f, 1, 4096))
        {
            s_State.Json["TileSizePixels"] = { std::max(1, tileSizePixels[0]), std::max(1, tileSizePixels[1]) };
            (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
        }
    }
}
