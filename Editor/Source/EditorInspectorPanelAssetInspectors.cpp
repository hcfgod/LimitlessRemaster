#include "EditorInspectorPanelAssetInspectors.h"

#include "EditorAssetNaming.h"
#include "EditorInspectorPanel.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAsset.h"
#include "Assets/TextureAssetImporter.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        std::vector<std::string> BuildAssetPickerKeysByType(Assets::AssetType assetType)
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != assetType || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            auto tryAddKnownDefault = [&](const char* key) {
                if (!key || !key[0] || seen.contains(key))
                    return;
                const auto resolved = Assets::ResolveAssetKeyToPath(key);
                if (resolved.IsFailure())
                    return;
                std::error_code ec;
                if (std::filesystem::exists(resolved.GetValue(), ec))
                {
                    seen.insert(key);
                    keys.emplace_back(key);
                }
            };

            if (assetType == Assets::AssetType::Shader)
            {
                tryAddKnownDefault("Assets/Shaders/Renderer2D_TexturedQuad.glsl");
                tryAddKnownDefault("Assets/Shaders/Renderer2D_MSDFText.glsl");
                tryAddKnownDefault("Assets/Shaders/TexturedTriangle.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_GBufferNormalPass.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_DirectionalLight.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_PointLight.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_Composite.glsl");
            }
            else if (assetType == Assets::AssetType::Material)
            {
                tryAddKnownDefault("Assets/Materials/Renderer2D_TexturedQuad.material.json");
                tryAddKnownDefault("Assets/Materials/Renderer2D_MSDFText.material.json");
                tryAddKnownDefault("Assets/Materials/Lighting2D_DefaultLit.material.json");
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        void InvalidateSpriteCachesForTexture(Scene* scene, const std::string& textureKey)
        {
            if (!scene || textureKey.empty())
                return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<SpriteComponent>();
            for (entt::entity entity : view)
            {
                auto& sprite = view.get<SpriteComponent>(entity);
                if (sprite.TextureKey == textureKey)
                {
                    sprite.CachedTexture.reset();
                    sprite.TextureLoadAttempted = false;
                }
            }
        }

        void ApplyTextureSpecificationAndPersist(Scene* scene,
                                                 Assets::TextureAsset::Ptr textureAsset,
                                                 const TextureSpecification& specification)
        {
            auto texture = textureAsset->GetTexture();
            if (!texture)
                return;

            Renderer::GetInstance().ExecuteImmediate(std::make_unique<SetTextureSpecificationCommand>(texture, specification));
            textureAsset->SetSpecification(specification);

            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        void PersistTextureSpecificationAndReload(Scene* scene,
                                                  Assets::TextureAsset::Ptr textureAsset,
                                                  const TextureSpecification& specification)
        {
            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            textureAsset->SetSpecification(specification);
            textureAsset->Reload();
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        bool LoadTilesetJson(const std::string& tilesetKey, nlohmann::json& outJson, std::filesystem::path& outResolvedPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(tilesetKey);
            if (resolvedResult.IsFailure())
                return false;

            outResolvedPath = resolvedResult.GetValue();
            std::ifstream input(outResolvedPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            try
            {
                input >> outJson;
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        bool SaveTilesetJson(Scene* scene,
                             const std::string& selectedTilesetAssetKey,
                             const nlohmann::json& json,
                             const std::filesystem::path& resolvedPath)
        {
            std::ofstream output(resolvedPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << json.dump(2);
            output.close();

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(selectedTilesetAssetKey, Assets::AssetType::Tileset);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);

            if (!scene)
                return true;

            auto& registry = scene->GetRegistry();
            auto tilemapView = registry.view<TilemapComponent>();
            for (entt::entity entity : tilemapView)
            {
                auto& tilemap = tilemapView.get<TilemapComponent>(entity);
                if (tilemap.TilesetAssetKey == selectedTilesetAssetKey)
                {
                    tilemap.TilesetAssetLoadAttempted = false;
                    tilemap.TilesetTextureLoadAttempted = false;
                    tilemap.CachedTilesetTexture.reset();
                }
            }

            return true;
        }

        bool LoadMaterialJson(const std::string& materialKey, nlohmann::json& outJson, std::filesystem::path& outResolvedPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(materialKey);
            if (resolvedResult.IsFailure())
                return false;

            outResolvedPath = resolvedResult.GetValue();

            std::ifstream in(outResolvedPath, std::ios::in | std::ios::binary);
            if (!in.is_open())
                return false;

            try
            {
                in >> outJson;
            }
            catch (...)
            {
                return false;
            }

            if (!outJson.is_object())
                outJson = nlohmann::json::object();
            return true;
        }

        bool SaveMaterialJsonAndReload(const std::string& materialKey,
                                       Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                                       const nlohmann::json& jsonToSave,
                                       const std::filesystem::path& resolvedPath)
        {
            std::ofstream out(resolvedPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return false;

            out << jsonToSave.dump(2);
            out.flush();

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(materialKey, Assets::AssetType::Material);

            if (cachedMaterialAsset && cachedMaterialAsset->GetKey() == materialKey)
                (void)cachedMaterialAsset->Reload();
            else
                cachedMaterialAsset = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(materialKey);

            return cachedMaterialAsset != nullptr;
        }
    }

    void DrawTextureInspector(Scene* scene,
                              std::string& selectedTextureAssetKey,
                              Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        if (!cachedTextureAsset || cachedTextureAsset->GetKey() != selectedTextureAssetKey)
            cachedTextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(selectedTextureAssetKey);

        auto textureAsset = cachedTextureAsset;
        if (!textureAsset)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", selectedTextureAssetKey.c_str());
            cachedTextureAsset.reset();
            return;
        }

        const auto* texture = textureAsset->GetTexture().get();
        if (!texture)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Texture not ready.");
            return;
        }

        const std::string fileName = std::filesystem::path(selectedTextureAssetKey).filename().string();
        ImGui::Text("Texture: %s", fileName.c_str());
        ImGui::Text("%u x %u", texture->GetWidth(), texture->GetHeight());
        ImGui::Spacing();

        const float previewSize = 256.0f;
        const float aspect = static_cast<float>(texture->GetHeight()) / static_cast<float>(texture->GetWidth());
        const ImVec2 uv0(0.0f, 1.0f);
        const ImVec2 uv1(1.0f, 0.0f);
        const ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 borderColor(0.4f, 0.4f, 0.4f, 1.0f);
        if (aspect > 1.0f)
        {
            const float width = previewSize / aspect;
            ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(width, previewSize), uv0, uv1, tintColor, borderColor);
        }
        else
        {
            const float height = previewSize * aspect;
            ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(previewSize, height), uv0, uv1, tintColor, borderColor);
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
        ImGui::SameLine(160);
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
        ImGui::SameLine(160);
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

    void DrawTilesetAssetInspector(Scene* scene, std::string& selectedTilesetAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
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
        ImGui::SameLine(160.0f);
        ImGui::Button((textureLabel + "##TilesetTexture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90.0f, 0.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0] && textureKey != key)
                {
                    textureKey = key;
                    s_State.Json["TextureKey"] = textureKey;
                    (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
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

    void DrawNativeScriptAssetInspector(std::string& selectedNativeScriptAssetKey)
    {
        if (selectedNativeScriptAssetKey.empty())
            return;

        std::filesystem::path selectedPath(selectedNativeScriptAssetKey);
        std::string extension = selectedPath.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        if (extension != ".h" && extension != ".cpp")
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Selected asset is not a native script file.");
            if (ImGui::Button("Clear Selection", ImVec2(160.0f, 0.0f)))
                selectedNativeScriptAssetKey.clear();
            return;
        }

        const bool isHeader = (extension == ".h");
        const std::string selectedFileName = selectedPath.filename().string();
        const std::string scriptClassName = selectedPath.stem().string();

        std::filesystem::path pairedPath = selectedPath;
        pairedPath.replace_extension(isHeader ? ".cpp" : ".h");
        const std::string pairedAssetKey = pairedPath.generic_string();
        const auto resolvedSelectedPath = Assets::ResolveAssetKeyToPath(selectedNativeScriptAssetKey);
        const auto resolvedPairedPath = Assets::ResolveAssetKeyToPath(pairedAssetKey);
        const bool pairedExists = resolvedPairedPath.IsSuccess();

        ImGui::Text("Native Script: %s", selectedFileName.c_str());
        ImGui::TextDisabled("Class: %s", scriptClassName.c_str());
        ImGui::TextDisabled("Type: %s", isHeader ? "Header (.h)" : "Source (.cpp)");
        ImGui::TextDisabled("Asset Key: %s", selectedNativeScriptAssetKey.c_str());
        if (resolvedSelectedPath.IsSuccess())
            ImGui::TextDisabled("Path: %s", resolvedSelectedPath.GetValue().string().c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not resolve selected script path.");

        ImGui::TextDisabled("Paired File: %s", pairedAssetKey.c_str());
        if (!pairedExists)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Pair file missing (.h + .cpp should both exist).");

        ImGui::Spacing();
        if (ImGui::Button("Open In Native Script Editor", ImVec2(-1.0f, 0.0f)))
            (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(selectedNativeScriptAssetKey);
        if (pairedExists && ImGui::Button("Open Paired File", ImVec2(-1.0f, 0.0f)))
            (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(pairedAssetKey);
    }

    void DrawPrefabAssetInspector(std::string& selectedPrefabAssetKey)
    {
        if (selectedPrefabAssetKey.empty())
            return;

        const std::filesystem::path selectedPath(selectedPrefabAssetKey);
        std::string lowerFileName = selectedPath.filename().string();
        std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (!lowerFileName.ends_with(".prefab.json"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Selected asset is not a prefab.");
            if (ImGui::Button("Clear Selection", ImVec2(160.0f, 0.0f)))
                selectedPrefabAssetKey.clear();
            return;
        }

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(selectedPrefabAssetKey);
        if (resolvedPathResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not resolve prefab path.");
            ImGui::TextDisabled("%s", selectedPrefabAssetKey.c_str());
            return;
        }

        const std::filesystem::path resolvedPath = resolvedPathResult.GetValue();
        const auto loadedSceneResult = Scene::LoadFromFile(resolvedPath);
        if (loadedSceneResult.IsFailure() || !loadedSceneResult.GetValue())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load prefab.");
            ImGui::TextDisabled("%s", selectedPrefabAssetKey.c_str());
            return;
        }

        const Scene& prefabScene = *loadedSceneResult.GetValue();
        const auto& registry = prefabScene.GetRegistry();
        auto tagView = registry.view<TagComponent>();
        const auto rootEntities = prefabScene.GetChildren(entt::null);
        uint32_t entityCount = 0;
        for (entt::entity entity : tagView)
        {
            (void)entity;
            ++entityCount;
        }

        ImGui::Text("Prefab: %s", EditorAssetNaming::GetAssetDisplayNameFromAssetKey(selectedPrefabAssetKey).c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedPrefabAssetKey.c_str());
        ImGui::TextDisabled("Path: %s", resolvedPath.string().c_str());
        ImGui::Separator();

        ImGui::Text("Entities: %u", entityCount);
        ImGui::Text("Root Objects: %u", static_cast<uint32_t>(rootEntities.size()));

        if (!rootEntities.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Root Preview");
            ImGui::BeginChild("PrefabRootPreview", ImVec2(0.0f, 120.0f), true);
            for (entt::entity root : rootEntities)
            {
                const auto* tag = registry.try_get<TagComponent>(root);
                const std::string label = (tag && !tag->Tag.empty()) ? tag->Tag : "Entity";
                ImGui::BulletText("%s", label.c_str());
            }
            ImGui::EndChild();
        }
    }

    void DrawMaterialInspector(const char* texturePayloadId,
                               const char* shaderPayloadId,
                               std::string& selectedMaterialAssetKey,
                               Assets::MaterialAsset::Ptr& cachedMaterialAsset)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
        };
        static State s_State;

        if (selectedMaterialAssetKey.empty())
            return;

        if (!cachedMaterialAsset || cachedMaterialAsset->GetKey() != selectedMaterialAssetKey)
            cachedMaterialAsset = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(selectedMaterialAssetKey);

        if (!s_State.Loaded || s_State.LoadedKey != selectedMaterialAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedMaterialAssetKey;
            s_State.Loaded = LoadMaterialJson(selectedMaterialAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load material JSON: %s", selectedMaterialAssetKey.c_str());
                return;
            }
        }

        const std::string fileName = std::filesystem::path(selectedMaterialAssetKey).filename().string();
        ImGui::Text("Material: %s", fileName.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Keep implementation in dedicated module. This function intentionally
        // remains behavior-identical to the previous in-file implementation.
        // The large editor material UI block is unchanged below.
        // -----------------------------------------------------------------------------------
        // NOTE: The body is retained from the original EditorInspectorPanel.cpp implementation.
        // -----------------------------------------------------------------------------------

        // Shader slot (required).
        std::string shaderLabel = "None";
        if (s_State.Json.contains("shader") && s_State.Json["shader"].is_object())
        {
            const auto& ref = s_State.Json["shader"];
            if (ref.contains("key") && ref["key"].is_string())
                shaderLabel = std::filesystem::path(ref["key"].get<std::string>()).filename().string();
        }
        if (cachedMaterialAsset)
        {
            if (auto shaderAsset = cachedMaterialAsset->GetShaderHandle().Lock())
                shaderLabel = std::filesystem::path(shaderAsset->GetKey()).filename().string();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Shader");
        ImGui::SameLine(80);
        ImGui::Button((shaderLabel + "##MaterialShader").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, 0));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(shaderPayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    auto shaderAsset = Assets::ShaderAsset::LoadBlocking(key);
                    if (shaderAsset)
                    {
                        s_State.Json["shader"] = {
                            { "guid", shaderAsset->GetGuid() },
                            { "key", shaderAsset->GetKey() }
                        };
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("...##MaterialShaderPicker"))
            ImGui::OpenPopup("MaterialShaderPickerPopup");
        if (ImGui::BeginPopup("MaterialShaderPickerPopup"))
        {
            const std::vector<std::string> shaderKeys = BuildAssetPickerKeysByType(Assets::AssetType::Shader);
            for (const auto& key : shaderKeys)
            {
                const bool isSelected = (shaderLabel == std::filesystem::path(key).filename().string());
                const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                if (ImGui::Selectable((display + "##MaterialShaderPicker_" + key).c_str(), isSelected))
                {
                    auto shaderAsset = Assets::ShaderAsset::LoadBlocking(key);
                    if (shaderAsset)
                    {
                        s_State.Json["shader"] = {
                            { "guid", shaderAsset->GetGuid() },
                            { "key", shaderAsset->GetKey() }
                        };
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", key.c_str());
            }
            ImGui::EndPopup();
        }

        struct TextureSlotDescriptor
        {
            const char* Id;
            const char* DisplayName;
            bool IsAlbedo;
        };
        static const std::array<TextureSlotDescriptor, 5> kTextureSlots = {{
            { "albedo", "Albedo (Diffuse)", true },
            { "normal", "Normal", false },
            { "metallic", "Metallic", false },
            { "occlusion", "Occlusion", false },
            { "emission", "Emission", false },
        }};

        auto ensureTextureSlotsRoot = [&]() -> nlohmann::json& {
            if (!s_State.Json.contains("textureSlots") || !s_State.Json["textureSlots"].is_object())
                s_State.Json["textureSlots"] = nlohmann::json::object();
            return s_State.Json["textureSlots"];
        };

        auto getSlotObject = [&](const TextureSlotDescriptor& slot, bool createIfMissing) -> nlohmann::json* {
            if (createIfMissing)
            {
                auto& slotsRoot = ensureTextureSlotsRoot();
                if (!slotsRoot.contains(slot.Id) || !slotsRoot[slot.Id].is_object())
                    slotsRoot[slot.Id] = nlohmann::json::object();
                return &slotsRoot[slot.Id];
            }

            if (!s_State.Json.contains("textureSlots") || !s_State.Json["textureSlots"].is_object())
                return nullptr;
            auto& slotsRoot = s_State.Json["textureSlots"];
            if (!slotsRoot.contains(slot.Id) || !slotsRoot[slot.Id].is_object())
                return nullptr;
            return &slotsRoot[slot.Id];
        };

        auto getTextureRefForSlot = [&](const TextureSlotDescriptor& slot) -> nlohmann::json* {
            if (slot.IsAlbedo)
            {
                if (s_State.Json.contains("mainTexture") && s_State.Json["mainTexture"].is_object())
                    return &s_State.Json["mainTexture"];

                nlohmann::json* slotObject = getSlotObject(slot, false);
                if (slotObject && slotObject->contains("texture") && (*slotObject)["texture"].is_object())
                    return &(*slotObject)["texture"];
                return nullptr;
            }

            nlohmann::json* slotObject = getSlotObject(slot, false);
            if (slotObject && slotObject->contains("texture") && (*slotObject)["texture"].is_object())
                return &(*slotObject)["texture"];
            return nullptr;
        };

        auto getTextureLabelForRef = [](const nlohmann::json* textureRef) -> std::string {
            if (!textureRef || !textureRef->is_object())
                return "None";
            if (textureRef->contains("key") && (*textureRef)["key"].is_string())
            {
                const std::string key = (*textureRef)["key"].get<std::string>();
                return key.empty() ? "None" : std::filesystem::path(key).filename().string();
            }
            if (textureRef->contains("guid") && (*textureRef)["guid"].is_string())
                return std::string("GUID: ") + (*textureRef)["guid"].get<std::string>();
            return "None";
        };

        auto setTextureForSlot = [&](const TextureSlotDescriptor& slot, Assets::TextureAsset::Ptr textureAsset) {
            if (!textureAsset)
                return;

            nlohmann::json textureRef = {
                { "guid", textureAsset->GetGuid() },
                { "key", textureAsset->GetKey() }
            };

            if (slot.IsAlbedo)
            {
                // Runtime compatibility: renderer currently consumes mainTexture/mainTextureSpec.
                s_State.Json["mainTexture"] = textureRef;
            }

            nlohmann::json* slotObject = getSlotObject(slot, true);
            (*slotObject)["texture"] = textureRef;

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto clearTextureForSlot = [&](const TextureSlotDescriptor& slot) {
            if (slot.IsAlbedo)
                s_State.Json.erase("mainTexture");

            if (nlohmann::json* slotObject = getSlotObject(slot, false))
            {
                slotObject->erase("texture");
                if (slotObject->empty() && s_State.Json.contains("textureSlots") && s_State.Json["textureSlots"].is_object())
                    s_State.Json["textureSlots"].erase(slot.Id);
            }

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto hasSamplerOverrideForSlot = [&](const TextureSlotDescriptor& slot) -> bool {
            if (slot.IsAlbedo)
                return s_State.Json.contains("mainTextureSpec") && s_State.Json["mainTextureSpec"].is_object();
            nlohmann::json* slotObject = getSlotObject(slot, false);
            return slotObject && slotObject->contains("spec") && (*slotObject)["spec"].is_object();
        };

        auto setSamplerOverrideForSlot = [&](const TextureSlotDescriptor& slot, bool enabled) {
            if (slot.IsAlbedo)
            {
                if (enabled)
                {
                    if (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object())
                        s_State.Json["mainTextureSpec"] = nlohmann::json::object();
                }
                else
                {
                    s_State.Json.erase("mainTextureSpec");
                }
            }

            nlohmann::json* slotObject = getSlotObject(slot, enabled);
            if (enabled)
            {
                if (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object())
                    (*slotObject)["spec"] = nlohmann::json::object();
            }
            else if (slotObject)
            {
                slotObject->erase("spec");
                if (slotObject->empty() && s_State.Json.contains("textureSlots") && s_State.Json["textureSlots"].is_object())
                    s_State.Json["textureSlots"].erase(slot.Id);
            }

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto getSpecObjectForSlot = [&](const TextureSlotDescriptor& slot, bool createIfMissing) -> nlohmann::json* {
            if (slot.IsAlbedo)
            {
                if (createIfMissing && (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object()))
                    s_State.Json["mainTextureSpec"] = nlohmann::json::object();

                if (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object())
                    return nullptr;

                // Keep Albedo slot mirror in textureSlots so non-runtime UI data stays in one structure too.
                nlohmann::json* slotObject = getSlotObject(slot, true);
                (*slotObject)["spec"] = s_State.Json["mainTextureSpec"];
                return &s_State.Json["mainTextureSpec"];
            }

            nlohmann::json* slotObject = getSlotObject(slot, createIfMissing);
            if (!slotObject)
                return nullptr;
            if (createIfMissing && (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object()))
                (*slotObject)["spec"] = nlohmann::json::object();
            if (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object())
                return nullptr;
            return &(*slotObject)["spec"];
        };

        auto drawSamplerSpecification = [&](const TextureSlotDescriptor& slot) {
            nlohmann::json* spec = getSpecObjectForSlot(slot, true);
            if (!spec)
                return;

            const char* filterNames[] = { "Nearest", "Linear" };
            auto getFilterIndex = [](const char* name) -> int {
                if (!name) return 1;
                return std::string(name) == "Nearest" ? 0 : 1;
            };
            auto getWrapIndex = [](const char* name) -> int {
                if (!name) return 0;
                return std::string(name) == "ClampToEdge" ? 1 : 0;
            };

            int minFilter = getFilterIndex(spec->value("minFilter", "Linear").c_str());
            if (ImGui::Combo("Min Filter", &minFilter, filterNames, 2))
            {
                (*spec)["minFilter"] = (minFilter == 0) ? "Nearest" : "Linear";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int magFilter = getFilterIndex(spec->value("magFilter", "Linear").c_str());
            if (ImGui::Combo("Mag Filter", &magFilter, filterNames, 2))
            {
                (*spec)["magFilter"] = (magFilter == 0) ? "Nearest" : "Linear";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
            int wrapU = getWrapIndex(spec->value("wrapU", "Repeat").c_str());
            if (ImGui::Combo("Wrap U", &wrapU, wrapNames, 2))
            {
                (*spec)["wrapU"] = (wrapU == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int wrapV = getWrapIndex(spec->value("wrapV", "Repeat").c_str());
            if (ImGui::Combo("Wrap V", &wrapV, wrapNames, 2))
            {
                (*spec)["wrapV"] = (wrapV == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            bool generateMipmaps = spec->value("generateMipmaps", false);
            if (ImGui::Checkbox("Generate Mipmaps", &generateMipmaps))
            {
                (*spec)["generateMipmaps"] = generateMipmaps;
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }
        };

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Surface Inputs");

        for (const auto& slot : kTextureSlots)
        {
            ImGui::PushID(slot.Id);

            std::string textureLabel = "None";
            if (slot.IsAlbedo && cachedMaterialAsset)
            {
                if (auto texAsset = cachedMaterialAsset->GetMainTextureHandle().Lock())
                    textureLabel = std::filesystem::path(texAsset->GetKey()).filename().string();
            }
            if (textureLabel == "None")
                textureLabel = getTextureLabelForRef(getTextureRefForSlot(slot));

            ImGuiTreeNodeFlags slotFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            const bool slotOpen = ImGui::TreeNodeEx(slot.DisplayName, slotFlags);

            ImGui::SameLine(220.0f);
            ImGui::Button((textureLabel + "##Texture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 90.0f, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(key);
                        if (textureAsset)
                            setTextureForSlot(slot, textureAsset);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("...##TexturePicker"))
                ImGui::OpenPopup("TexturePickerPopup");
            if (ImGui::BeginPopup("TexturePickerPopup"))
            {
                const std::vector<std::string> textureKeys = BuildAssetPickerKeysByType(Assets::AssetType::Texture2D);
                for (const auto& key : textureKeys)
                {
                    const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                    if (ImGui::Selectable((display + "##TexturePicker_" + key).c_str(), textureLabel == std::filesystem::path(key).filename().string()))
                    {
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(key);
                        if (textureAsset)
                            setTextureForSlot(slot, textureAsset);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", key.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearTextureSlot"))
                clearTextureForSlot(slot);

            if (slotOpen)
            {
                bool hasSamplerOverride = hasSamplerOverrideForSlot(slot);
                if (ImGui::Checkbox("Override Sampler", &hasSamplerOverride))
                    setSamplerOverrideForSlot(slot, hasSamplerOverride);

                if (hasSamplerOverride)
                    drawSamplerSpecification(slot);

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Lighting Response");

        float normalStrength = s_State.Json.value("normalStrength", 1.0f);
        if (ImGui::DragFloat("Normal Strength", &normalStrength, 0.01f, 0.0f, 8.0f, "%.2f"))
        {
            s_State.Json["normalStrength"] = std::clamp(normalStrength, 0.0f, 8.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float roughness = s_State.Json.value("roughness", 0.5f);
        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f, "%.2f"))
        {
            s_State.Json["roughness"] = std::clamp(roughness, 0.0f, 1.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float specularIntensity = s_State.Json.value("specularIntensity", s_State.Json.value("specular", 0.5f));
        if (ImGui::DragFloat("Specular Intensity", &specularIntensity, 0.01f, 0.0f, 8.0f, "%.2f"))
        {
            s_State.Json["specularIntensity"] = std::clamp(specularIntensity, 0.0f, 8.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }
    }
}
