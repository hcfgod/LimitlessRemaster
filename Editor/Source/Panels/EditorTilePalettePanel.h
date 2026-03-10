#pragma once

#include "EditorViewportPanel.h"
#include "Assets/TilePaletteAsset.h"
#include "Assets/TextureAsset.h"

#include <cstdint>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    class Scene;
    class EditorUndoService;
}

namespace Limitless::EditorTilePalettePanel
{
    /// Persistent state for the Tile Palette panel, held by EditorLayer.
    struct TilePaletteState
    {
        bool PanelOpen = true;

        /// Currently selected palette asset key.
        std::string ActivePaletteKey;
        std::vector<std::string> CachedPaletteKeys;
        bool PaletteKeysDirty = true;
        uint64_t CachedPaletteKeysRevision = 0;

        /// Cached palette data loaded from disk.
        Assets::TilePaletteData CachedPaletteData;
        bool PaletteDataLoaded = false;

        /// Cached textures for rendering palette tiles (keyed by texture asset key).
        std::unordered_map<std::string, Assets::TextureAsset::Ptr> CachedTextures;

        /// Cached per-tile UV data (parallel to CachedPaletteData.TileAssetKeys).
        struct TileRenderInfo
        {
            std::string TextureKey;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
        };
        std::vector<TileRenderInfo> CachedTileRenderInfo;
        bool TileRenderInfoDirty = true;

        /// Selected tile range in palette grid coordinates.
        glm::ivec2 SelectionStart = glm::ivec2(-1);
        glm::ivec2 SelectionEnd   = glm::ivec2(-1);
        bool Selecting = false;
        bool DragConfirmed = false;  // True once mouse moves to a different tile during drag.
        glm::ivec2 ClickOriginTile = glm::ivec2(-1); // Tile where mouse-down occurred.

        /// Reference to the shared tilemap editor state for painting.
        EditorViewportPanel::TilemapEditorState* TilemapEditorStatePtr = nullptr;

        /// The grid entity currently selected in the scene (if any).
        entt::entity ActiveGridEntity = entt::null;

        /// The layer entity currently being painted on.
        entt::entity ActiveLayerEntity = entt::null;

        void InvalidateCache()
        {
            PaletteDataLoaded = false;
            TileRenderInfoDirty = true;
            CachedTileRenderInfo.clear();
            CachedTextures.clear();
        }
    };

    /// Draw the Tile Palette panel. Called each frame from EditorLayer::OnRender().
    void DrawTilePalettePanel(TilePaletteState& state,
                              Scene* scene,
                              entt::entity selectedEntity,
                              EditorViewportPanel::TilemapEditorState& tilemapEditorState,
                              EditorUndoService* undoService);

    /// Call after creating or deleting a TilePalette asset so the dropdown refreshes.
    void InvalidatePaletteKeyCache(TilePaletteState& state);
}
