#include "EditorTilePalettePanel.h"

#include "EditorPanelStyle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/TileAsset.h"
#include "Assets/TilePaletteAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Debug/Log.h"
#include "Graphics/NativeRenderHandles.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace Limitless::EditorTilePalettePanel
{
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    static constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";
    static constexpr const char* kTexturePayloadId   = "ASSET_TEXTURE";

    /// Cached palette asset key list so we don't scan the database every frame.
    static std::vector<std::string> s_CachedPaletteKeys;
    static bool s_PaletteKeysDirty = true;
    static uint64_t s_CachedPaletteKeysRevision = 0;

    static bool IsAssetKeyUnderOpenProjectAssets(const std::string& assetKey)
    {
        if (assetKey.empty())
            return false;

        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return true;

        const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolvedResult.IsFailure())
            return false;

        std::error_code ec;
        const std::filesystem::path resolvedPath = std::filesystem::weakly_canonical(resolvedResult.GetValue(), ec);
        if (ec)
            return false;

        ec.clear();
        if (!std::filesystem::exists(resolvedPath, ec))
            return false;

        ec.clear();
        const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(projectManager.GetProjectRoot() / "Assets", ec);
        if (ec)
            return false;

        ec.clear();
        const std::filesystem::path rel = std::filesystem::relative(resolvedPath, assetsRoot, ec);
        if (ec)
            return false;
        if (rel.empty())
            return true;

        const std::string relText = rel.generic_string();
        return !(relText == ".." || relText.rfind("../", 0) == 0);
    }

    static const std::vector<std::string>& GetCachedPaletteAssetKeys()
    {
        const uint64_t currentDatabaseRevision = Assets::AssetDatabase::GetInstance().GetRevision();
        if (s_PaletteKeysDirty || currentDatabaseRevision != s_CachedPaletteKeysRevision)
        {
            s_CachedPaletteKeys.clear();
            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::TilePalette)
                    continue;
                if (IsAssetKeyUnderOpenProjectAssets(record.Key))
                    s_CachedPaletteKeys.push_back(record.Key);
            }
            std::sort(s_CachedPaletteKeys.begin(), s_CachedPaletteKeys.end());
            s_CachedPaletteKeysRevision = currentDatabaseRevision;
            s_PaletteKeysDirty = false;
        }
        return s_CachedPaletteKeys;
    }

    /// Rebuild the per-tile render info cache from the loaded palette data.
    /// Uses a fast path when all tiles come from the same source sprite sheet.
    static void RebuildTileRenderInfoCache(TilePaletteState& state)
    {
        const auto& paletteData = state.CachedPaletteData;
        state.CachedTileRenderInfo.clear();
        state.CachedTileRenderInfo.resize(paletteData.TileAssetKeys.size());

        // Fast path: palette was populated from a single sprite sheet.
        // Compute all UVs from the shared sprite import settings in one pass
        // instead of reading each .tile.json individually.
        if (!paletteData.SourceTextureKey.empty())
        {
            const std::string& textureKey = paletteData.SourceTextureKey;

            auto& cachedTex = state.CachedTextures[textureKey];
            if (!cachedTex)
            {
                cachedTex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(textureKey));
                if (!cachedTex)
                    (void)Assets::TextureAsset::LoadAsync(textureKey);
            }

            if (!cachedTex || !cachedTex->GetTexture())
            {
                state.TileRenderInfoDirty = true;
                return;
            }

            const auto spriteSettings = Assets::LoadSpriteImportSettings(textureKey);
            const uint32_t texW = cachedTex->GetTexture()->GetWidth();
            const uint32_t texH = cachedTex->GetTexture()->GetHeight();

            for (size_t i = 0; i < paletteData.TileAssetKeys.size(); ++i)
            {
                if (paletteData.TileAssetKeys[i].empty())
                    continue;

                TilePaletteState::TileRenderInfo info;
                info.TextureKey = textureKey;

                const int32_t subIdx = static_cast<int32_t>(i);
                if (subIdx < static_cast<int32_t>(spriteSettings.SubSprites.size()))
                {
                    const auto& rect = spriteSettings.SubSprites[static_cast<size_t>(subIdx)].RectPixels;
                    const glm::vec4 uvs = Assets::ComputeSubSpriteUvs(rect, texW, texH);
                    info.UvMin = glm::vec2(uvs.x, uvs.y);
                    info.UvMax = glm::vec2(uvs.z, uvs.w);
                }

                state.CachedTileRenderInfo[i] = info;
            }

            state.TileRenderInfoDirty = false;
            return;
        }

        // Slow fallback: load tile assets individually.
        // Batch sprite settings by texture key to avoid redundant disk reads.
        bool allTexturesResolved = true;
        std::unordered_map<std::string, Assets::SpriteImportSettings> settingsCache;

        for (size_t i = 0; i < paletteData.TileAssetKeys.size(); ++i)
        {
            const std::string& tileKey = paletteData.TileAssetKeys[i];
            if (tileKey.empty())
                continue;

            auto tileResult = Assets::LoadTileAssetData(tileKey);
            if (tileResult.IsFailure())
                continue;

            const Assets::TileAssetData& tile = tileResult.GetValue();
            TilePaletteState::TileRenderInfo info;
            info.TextureKey = tile.SpriteTextureKey;

            if (tile.SubSpriteIndex >= 0 && !tile.SpriteTextureKey.empty())
            {
                auto settingsIt = settingsCache.find(tile.SpriteTextureKey);
                if (settingsIt == settingsCache.end())
                    settingsIt = settingsCache.emplace(tile.SpriteTextureKey,
                        Assets::LoadSpriteImportSettings(tile.SpriteTextureKey)).first;
                const auto& spriteSettings = settingsIt->second;

                if (tile.SubSpriteIndex < static_cast<int32_t>(spriteSettings.SubSprites.size()))
                {
                    auto& cachedTex = state.CachedTextures[tile.SpriteTextureKey];
                    if (!cachedTex)
                    {
                        cachedTex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                            Assets::AssetManager::GetCachedByKey(tile.SpriteTextureKey));
                        if (!cachedTex)
                            (void)Assets::TextureAsset::LoadAsync(tile.SpriteTextureKey);
                    }

                    if (cachedTex && cachedTex->GetTexture())
                    {
                        const auto& rect = spriteSettings.SubSprites[static_cast<size_t>(tile.SubSpriteIndex)].RectPixels;
                        const glm::vec4 uvs = Assets::ComputeSubSpriteUvs(
                            rect,
                            cachedTex->GetTexture()->GetWidth(),
                            cachedTex->GetTexture()->GetHeight());
                        info.UvMin = glm::vec2(uvs.x, uvs.y);
                        info.UvMax = glm::vec2(uvs.z, uvs.w);
                    }
                    else
                    {
                        allTexturesResolved = false;
                    }
                }
            }
            else if (!tile.SpriteTextureKey.empty())
            {
                auto& cachedTex = state.CachedTextures[tile.SpriteTextureKey];
                if (!cachedTex)
                {
                    cachedTex = std::dynamic_pointer_cast<Assets::TextureAsset>(
                        Assets::AssetManager::GetCachedByKey(tile.SpriteTextureKey));
                    if (!cachedTex)
                    {
                        (void)Assets::TextureAsset::LoadAsync(tile.SpriteTextureKey);
                        allTexturesResolved = false;
                    }
                }
                info.UvMin = glm::vec2(0.0f);
                info.UvMax = glm::vec2(1.0f);
            }

            state.CachedTileRenderInfo[i] = info;
        }

        state.TileRenderInfoDirty = !allTexturesResolved;
    }

    /// Handle a sprite sheet being dropped onto the palette.
    static void HandleSpriteSheetDrop(TilePaletteState& state, const std::string& subSpriteKey)
    {
        // Parse parent texture key from "Assets/Texture.png#index".
        const size_t hashPos = subSpriteKey.rfind('#');
        std::string textureKey;
        if (hashPos != std::string::npos)
            textureKey = subSpriteKey.substr(0, hashPos);
        else
            textureKey = subSpriteKey;

        if (textureKey.empty() || state.ActivePaletteKey.empty())
            return;

        Assets::TilePaletteData paletteData;
        const auto result = Assets::PopulatePaletteFromSpriteSheet(
            state.ActivePaletteKey, textureKey, paletteData);

        if (result.IsFailure())
        {
            const std::string errorMsg = result.GetError().GetErrorMessage();
            LT_CORE_WARN("TilePalettePanel: failed to populate palette: {}", errorMsg);
            return;
        }

        // Save the populated palette to disk.
        const auto saveResult = Assets::SaveTilePaletteData(state.ActivePaletteKey, paletteData);
        if (saveResult.IsFailure())
        {
            const std::string saveErr = saveResult.GetError().GetErrorMessage();
            LT_CORE_WARN("TilePalettePanel: failed to save palette: {}", saveErr);
        }

        state.CachedPaletteData = std::move(paletteData);
        state.PaletteDataLoaded = true;
        state.TileRenderInfoDirty = true;
        s_PaletteKeysDirty = true;
    }

    /// Find the Grid2D entity and active layer entity from the scene context.
    static void ResolveGridAndLayerEntities(TilePaletteState& state,
                                            Scene* scene,
                                            entt::entity selectedEntity)
    {
        if (!scene)
        {
            state.ActiveGridEntity = entt::null;
            state.ActiveLayerEntity = entt::null;
            return;
        }

        auto& registry = scene->GetRegistry();

        // Keep previously resolved targets if they are still valid.
        if (state.ActiveGridEntity != entt::null)
        {
            if (!scene->IsValid(state.ActiveGridEntity) ||
                !registry.all_of<Grid2DComponent>(state.ActiveGridEntity))
            {
                state.ActiveGridEntity = entt::null;
            }
        }
        if (state.ActiveLayerEntity != entt::null)
        {
            if (!scene->IsValid(state.ActiveLayerEntity) ||
                !registry.all_of<TilemapLayerComponent>(state.ActiveLayerEntity))
            {
                state.ActiveLayerEntity = entt::null;
            }
        }

        // If no valid scene selection, keep previous targets.
        if (selectedEntity == entt::null || !scene->IsValid(selectedEntity))
            return;

        // If selected entity itself has Grid2DComponent, use it directly.
        if (registry.all_of<Grid2DComponent>(selectedEntity))
        {
            state.ActiveGridEntity = selectedEntity;
            // Keep current layer if it still belongs to this grid.
            bool keptExistingLayer = false;
            if (state.ActiveLayerEntity != entt::null &&
                scene->GetParent(state.ActiveLayerEntity) == selectedEntity &&
                registry.all_of<TilemapLayerComponent>(state.ActiveLayerEntity))
            {
                keptExistingLayer = true;
            }

            if (!keptExistingLayer)
            {
                // Default to first child with TilemapLayerComponent.
                state.ActiveLayerEntity = entt::null;
                const auto children = scene->GetChildren(selectedEntity);
                for (entt::entity child : children)
                {
                    if (registry.all_of<TilemapLayerComponent>(child))
                    {
                        state.ActiveLayerEntity = child;
                        break;
                    }
                }
            }
            return;
        }

        // If selected entity has TilemapLayerComponent, find its Grid parent.
        if (registry.all_of<TilemapLayerComponent>(selectedEntity))
        {
            entt::entity parent = scene->GetParent(selectedEntity);
            if (parent != entt::null && scene->IsValid(parent) &&
                registry.all_of<Grid2DComponent>(parent))
            {
                state.ActiveGridEntity = parent;
                state.ActiveLayerEntity = selectedEntity;
            }
            return;
        }

        // If selected entity is a child of a grid, keep grid context and
        // preserve the last selected layer under that grid.
        entt::entity parent = scene->GetParent(selectedEntity);
        if (parent != entt::null && scene->IsValid(parent))
        {
            if (registry.all_of<Grid2DComponent>(parent))
            {
                state.ActiveGridEntity = parent;
                if (state.ActiveLayerEntity != entt::null &&
                    scene->GetParent(state.ActiveLayerEntity) != parent)
                {
                    state.ActiveLayerEntity = entt::null;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void DrawTilePalettePanel(TilePaletteState& state,
                              Scene* scene,
                              entt::entity selectedEntity,
                              EditorViewportPanel::TilemapEditorState& tilemapEditorState,
                              EditorUndoService* undoService)
    {
        state.TilemapEditorStatePtr = &tilemapEditorState;

        if (!state.PanelOpen)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Tile Palette", &state.PanelOpen))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        // ---- Palette selector ------------------------------------------------
        const auto& paletteKeys = GetCachedPaletteAssetKeys();

        {
            const std::string activeLabel = state.ActivePaletteKey.empty()
                ? std::string("(none)")
                : state.ActivePaletteKey;

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::BeginCombo("##PaletteSelector", activeLabel.c_str()))
            {
                for (const auto& key : paletteKeys)
                {
                    const bool isSelected = (key == state.ActivePaletteKey);
                    if (ImGui::Selectable(key.c_str(), isSelected))
                    {
                        if (key != state.ActivePaletteKey)
                        {
                            state.ActivePaletteKey = key;
                            state.InvalidateCache();
                        }
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        if (state.ActivePaletteKey.empty())
        {
            ImGui::TextDisabled("Select or create a Tile Palette asset.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        // ---- Load palette data lazily ----------------------------------------
        if (!state.PaletteDataLoaded)
        {
            auto loadResult = Assets::LoadTilePaletteData(state.ActivePaletteKey);
            if (loadResult.IsSuccess())
            {
                state.CachedPaletteData = std::move(loadResult.GetValue());
                state.PaletteDataLoaded = true;
                state.TileRenderInfoDirty = true;
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "Failed to load palette: %s",
                    loadResult.GetError().GetErrorMessage().c_str());
                ImGui::End();
                EditorPanelStyle::PopPanelVisualStyle();
                return;
            }
        }

        // ---- Rebuild render info cache ---------------------------------------
        if (state.TileRenderInfoDirty)
            RebuildTileRenderInfoCache(state);

        // ---- Resolve grid/layer from scene context ---------------------------
        ResolveGridAndLayerEntities(state, scene, selectedEntity);

        // ---- Paint controls --------------------------------------------------
        ImGui::Separator();
        ImGui::Checkbox("Enable Painting", &tilemapEditorState.Enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Grid Overlay", &tilemapEditorState.ShowGridOverlay);

        {
            int paintModeIndex = static_cast<int>(tilemapEditorState.PaintMode);
            const char* paintModeNames[] = { "Single", "Rectangle", "Fill", "Erase" };
            if (ImGui::Combo("Paint Mode", &paintModeIndex, paintModeNames, 4))
                tilemapEditorState.PaintMode = static_cast<EditorViewportPanel::TilemapPaintMode>(paintModeIndex);
        }

        ImGui::DragInt("Brush Size", &tilemapEditorState.BrushSize, 1.0f, 1, 128);
        tilemapEditorState.BrushSize = std::max(1, tilemapEditorState.BrushSize);

        // ---- Layer selector --------------------------------------------------
        if (state.ActiveGridEntity != entt::null && scene)
        {
            ImGui::Separator();
            const auto layerChildren = scene->GetChildren(state.ActiveGridEntity);
            std::vector<entt::entity> layerEntities;
            auto& registry = scene->GetRegistry();
            for (entt::entity child : layerChildren)
            {
                if (registry.all_of<TilemapLayerComponent>(child))
                    layerEntities.push_back(child);
            }

            if (!layerEntities.empty())
            {
                // Sort layers by render order.
                std::sort(layerEntities.begin(), layerEntities.end(),
                    [&registry](entt::entity a, entt::entity b) {
                        return registry.get<TilemapLayerComponent>(a).RenderOrder
                             < registry.get<TilemapLayerComponent>(b).RenderOrder;
                    });

                const auto* activeTag = (state.ActiveLayerEntity != entt::null)
                    ? registry.try_get<TagComponent>(state.ActiveLayerEntity)
                    : nullptr;
                const std::string activeLayerLabel = activeTag
                    ? activeTag->Tag
                    : std::string("(none)");

                if (ImGui::BeginCombo("Active Layer", activeLayerLabel.c_str()))
                {
                    for (entt::entity layerEntity : layerEntities)
                    {
                        const auto* tag = registry.try_get<TagComponent>(layerEntity);
                        const std::string label = tag ? tag->Tag : "Unnamed";
                        const bool isSelected = (layerEntity == state.ActiveLayerEntity);
                        if (ImGui::Selectable(label.c_str(), isSelected))
                            state.ActiveLayerEntity = layerEntity;
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }

        // Push explicit paint targets into shared viewport state so painting can
        // continue even when scene selection focus changes to non-grid entities.
        tilemapEditorState.ActiveGridEntity = state.ActiveGridEntity;
        tilemapEditorState.ActiveLayerEntity = state.ActiveLayerEntity;

        // Keep a stable one-line status area so panel layout never shifts while
        // selecting/dragging tiles.
        if (tilemapEditorState.HasHoveredCell)
            ImGui::Text("Cell: (%d, %d)", tilemapEditorState.HoveredCell.x, tilemapEditorState.HoveredCell.y);
        else
            ImGui::TextDisabled("Cell: (--, --)");

        // ---- Drag-drop target to populate palette ----------------------------
        ImGui::Separator();
        {
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float dropZoneHeight = 40.0f;

            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##PaletteDropZone", ImVec2(availWidth, dropZoneHeight));
            const bool isHovered = ImGui::IsItemHovered();

            ImU32 bgColor = isHovered
                ? IM_COL32(80, 130, 200, 80)
                : IM_COL32(60, 60, 60, 80);
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + availWidth, cursorPos.y + dropZoneHeight),
                bgColor, 4.0f);

            const char* dropText = "Drop a sprite sheet here to populate";
            ImVec2 textSize = ImGui::CalcTextSize(dropText);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(cursorPos.x + (availWidth - textSize.x) * 0.5f,
                       cursorPos.y + (dropZoneHeight - textSize.y) * 0.5f),
                IM_COL32(180, 180, 180, 255), dropText);

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                {
                    const std::string droppedKey(static_cast<const char*>(payload->Data));
                    HandleSpriteSheetDrop(state, droppedKey);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTexturePayloadId))
                {
                    const std::string droppedKey(static_cast<const char*>(payload->Data));
                    HandleSpriteSheetDrop(state, droppedKey);
                }
                ImGui::EndDragDropTarget();
            }
        }

        // ---- Tile grid -------------------------------------------------------
        const auto& palette = state.CachedPaletteData;
        if (palette.GridSize.x <= 0 || palette.GridSize.y <= 0 ||
            palette.TileAssetKeys.empty())
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        ImGui::Separator();

        const int32_t columns = palette.GridSize.x;
        const int32_t rows    = palette.GridSize.y;
        const int32_t totalTiles = columns * rows;

        // Always show stamp info to keep vertical layout stable.
        const int32_t stampW = std::max(1, tilemapEditorState.StampSize.x);
        const int32_t stampH = std::max(1, tilemapEditorState.StampSize.y);
        ImGui::TextDisabled("Stamp: %d x %d tiles", stampW, stampH);

        // Compute tile size to fill the available width, clamped to a reasonable range.
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float cellStride = std::max(17.0f, std::min(
            availWidth / static_cast<float>(columns), 129.0f));
        const float tileSize = cellStride - 1.0f; // 1px gap between tiles

        // Scrollable child region for the tile grid.
        const float childHeight = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##PaletteTileGrid", ImVec2(0.0f, childHeight), ImGuiChildFlags_None,
            ImGuiWindowFlags_HorizontalScrollbar);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();
        const float gridTotalWidth  = cellStride * static_cast<float>(columns);
        const float gridTotalHeight = cellStride * static_cast<float>(rows);

        // Reserve space for the full grid without capturing focus (Dummy doesn't scroll).
        ImGui::Dummy(ImVec2(gridTotalWidth, gridTotalHeight));

        // Detect mouse interaction on the child window directly to avoid scroll jumps.
        const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
        const ImVec2 mousePos = ImGui::GetMousePos();
        const int32_t hoveredCol = static_cast<int32_t>(
            std::floor((mousePos.x - gridOrigin.x) / cellStride));
        const int32_t hoveredRow = static_cast<int32_t>(
            std::floor((mousePos.y - gridOrigin.y) / cellStride));
        const bool mouseOverTile = windowHovered &&
            hoveredCol >= 0 && hoveredCol < columns &&
            hoveredRow >= 0 && hoveredRow < rows;

        // ---- Tile selection / drag logic ----------------------------------------
        // Single click = select one tile immediately.
        // Click-and-drag to a different cell = multi-tile stamp selection.
        // The key invariant: a click always selects its tile. Dragging only
        // begins once the mouse moves to a DIFFERENT cell while held down.

        auto commitSingleTile = [&](int32_t col, int32_t row) {
            const int32_t tileIdx = row * columns + col;
            const std::string tileKey = (tileIdx < static_cast<int32_t>(palette.TileAssetKeys.size()))
                ? palette.TileAssetKeys[static_cast<size_t>(tileIdx)] : std::string{};
            tilemapEditorState.SetSingleTile(
                static_cast<uint32_t>(tileIdx + 1), tileKey);
        };

        auto commitStamp = [&](const glm::ivec2& sMin, const glm::ivec2& sMax) {
            const int32_t stampW = sMax.x - sMin.x + 1;
            const int32_t stampH = sMax.y - sMin.y + 1;
            tilemapEditorState.StampSize = glm::ivec2(stampW, stampH);
            tilemapEditorState.StampTileIds.clear();
            tilemapEditorState.StampTileAssetKeys.clear();
            const size_t stampCells = static_cast<size_t>(stampW * stampH);
            tilemapEditorState.StampTileIds.reserve(stampCells);
            tilemapEditorState.StampTileAssetKeys.reserve(stampCells);

            for (int32_t sy = sMin.y; sy <= sMax.y; ++sy)
            {
                for (int32_t sx = sMin.x; sx <= sMax.x; ++sx)
                {
                    const int32_t paletteIndex = sy * columns + sx;
                    if (paletteIndex < totalTiles)
                    {
                        tilemapEditorState.StampTileIds.push_back(
                            static_cast<uint32_t>(paletteIndex + 1));
                        tilemapEditorState.StampTileAssetKeys.push_back(
                            (static_cast<size_t>(paletteIndex) < palette.TileAssetKeys.size())
                                ? palette.TileAssetKeys[static_cast<size_t>(paletteIndex)]
                                : std::string{});
                    }
                    else
                    {
                        tilemapEditorState.StampTileIds.push_back(0);
                        tilemapEditorState.StampTileAssetKeys.emplace_back();
                    }
                }
            }

            if (stampW == 1 && stampH == 1 && !tilemapEditorState.StampTileIds.empty())
            {
                tilemapEditorState.ActiveTileId = tilemapEditorState.StampTileIds[0];
                tilemapEditorState.ActiveTileAssetKey = tilemapEditorState.StampTileAssetKeys[0];
            }
        };

        // Mouse-down: record origin, select the single tile.
        if (ImGui::IsMouseClicked(0) && mouseOverTile)
        {
            state.ClickOriginTile = glm::ivec2(hoveredCol, hoveredRow);
            state.SelectionStart = state.ClickOriginTile;
            state.SelectionEnd   = state.ClickOriginTile;
            state.Selecting = false;
            state.DragConfirmed = false;
            commitSingleTile(hoveredCol, hoveredRow);
        }

        // Mouse held: only begin drag once the cursor moves to a different tile.
        if (state.ClickOriginTile.x >= 0 && ImGui::IsMouseDown(0) && mouseOverTile)
        {
            const glm::ivec2 currentTile(hoveredCol, hoveredRow);
            if (!state.DragConfirmed && currentTile != state.ClickOriginTile)
            {
                state.DragConfirmed = true;
                state.Selecting = true;
            }
            if (state.DragConfirmed)
                state.SelectionEnd = currentTile;
        }

        // Mouse released: finalize.
        if (state.ClickOriginTile.x >= 0 && ImGui::IsMouseReleased(0))
        {
            if (state.DragConfirmed && state.SelectionStart.x >= 0 && state.SelectionEnd.x >= 0)
            {
                const glm::ivec2 sMin = glm::min(state.SelectionStart, state.SelectionEnd);
                const glm::ivec2 sMax = glm::max(state.SelectionStart, state.SelectionEnd);
                commitStamp(sMin, sMax);
            }

            state.Selecting = false;
            state.DragConfirmed = false;
            state.ClickOriginTile = glm::ivec2(-1);
        }

        // Determine selection range for highlight.
        const bool hasSelection = state.SelectionStart.x >= 0 && state.SelectionEnd.x >= 0;
        const glm::ivec2 selMin = hasSelection
            ? glm::min(state.SelectionStart, state.SelectionEnd) : glm::ivec2(-1);
        const glm::ivec2 selMax = hasSelection
            ? glm::max(state.SelectionStart, state.SelectionEnd) : glm::ivec2(-1);

        // Draw tiles and selection highlights.
        for (int32_t tileIndex = 0; tileIndex < totalTiles; ++tileIndex)
        {
            const int32_t col = tileIndex % columns;
            const int32_t row = tileIndex / columns;

            const ImVec2 cellMin(
                gridOrigin.x + static_cast<float>(col) * cellStride,
                gridOrigin.y + static_cast<float>(row) * cellStride);
            const ImVec2 cellMax(cellMin.x + tileSize, cellMin.y + tileSize);

            const bool tileIsEmpty = (static_cast<size_t>(tileIndex) >= palette.TileAssetKeys.size() ||
                                       palette.TileAssetKeys[static_cast<size_t>(tileIndex)].empty());

            // Resolve texture and UVs.
            ImTextureID textureId = 0;
            ImVec2 uv0(0.0f, 0.0f);
            ImVec2 uv1(1.0f, 1.0f);

            if (static_cast<size_t>(tileIndex) < state.CachedTileRenderInfo.size())
            {
                const auto& info = state.CachedTileRenderInfo[static_cast<size_t>(tileIndex)];
                if (!info.TextureKey.empty())
                {
                    auto it = state.CachedTextures.find(info.TextureKey);
                    if (it == state.CachedTextures.end() || !it->second)
                    {
                        auto cached = std::dynamic_pointer_cast<Assets::TextureAsset>(
                            Assets::AssetManager::GetCachedByKey(info.TextureKey));
                        if (cached)
                            state.CachedTextures[info.TextureKey] = cached;
                        it = state.CachedTextures.find(info.TextureKey);
                    }
                    if (it != state.CachedTextures.end() && it->second && it->second->GetTexture())
                    {
                        textureId = static_cast<ImTextureID>(GetTextureNativeHandle(it->second->GetTexture()));
                        uv0 = ImVec2(info.UvMin.x, info.UvMin.y);
                        uv1 = ImVec2(info.UvMax.x, info.UvMax.y);
                    }
                }
            }

            if (textureId && !tileIsEmpty)
            {
                // Match Sprite Editor orientation (OpenGL texture space is flipped in Y).
                const ImVec2 drawUv0(uv0.x, 1.0f - uv0.y);
                const ImVec2 drawUv1(uv1.x, 1.0f - uv1.y);
                drawList->AddImage(textureId, cellMin, cellMax, drawUv0, drawUv1);
            }
            else
                drawList->AddRectFilled(cellMin, cellMax, IM_COL32(40, 40, 40, 255));

            // Selection highlight.
            const bool isInSelection = hasSelection &&
                col >= selMin.x && col <= selMax.x &&
                row >= selMin.y && row <= selMax.y;

            if (isInSelection)
                drawList->AddRect(cellMin, cellMax, IM_COL32(85, 200, 255, 255), 0.0f, 0, 2.0f);
        }

        // Tooltip for hovered tile.
        if (mouseOverTile)
        {
            const int32_t hoverIdx = hoveredRow * columns + hoveredCol;
            if (hoverIdx < static_cast<int32_t>(palette.TileAssetKeys.size()) &&
                !palette.TileAssetKeys[static_cast<size_t>(hoverIdx)].empty())
            {
                ImGui::SetTooltip("Tile %d: %s", hoverIdx + 1,
                    palette.TileAssetKeys[static_cast<size_t>(hoverIdx)].c_str());
            }
        }

        ImGui::EndChild();

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }

    void InvalidatePaletteKeyCache()
    {
        s_PaletteKeysDirty = true;
        s_CachedPaletteKeysRevision = 0;
    }
}
