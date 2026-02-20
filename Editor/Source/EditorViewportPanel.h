#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "EditorPlayMode.h"
#include "EnTT/entt.hpp"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class Camera;
    class EditorCameraController;
    class EditorUndoService;
    class Framebuffer;
    class Scene;
}

namespace Limitless::EditorViewportPanel
{
    enum class TilemapPaintMode : uint8_t
    {
        Single = 0,
        Rectangle = 1,
        Fill = 2,
        Erase = 3
    };

    struct TilemapEditorState
    {
        bool Enabled = true;
        bool ShowGridOverlay = true;
        TilemapPaintMode PaintMode = TilemapPaintMode::Single;
        int32_t BrushSize = 1;
        uint32_t ActiveTileId = 1;
        bool HasHoveredCell = false;
        glm::ivec2 HoveredCell = glm::ivec2(0, 0);

        // Preferred Grid2D/layer paint targets pushed by the Tile Palette panel.
        // When valid, viewport painting uses these instead of relying solely on
        // scene selection heuristics.
        entt::entity ActiveGridEntity = entt::null;
        entt::entity ActiveLayerEntity = entt::null;

        // Multi-tile stamp brush: a rectangular group of tile IDs painted as one.
        glm::ivec2 StampSize = glm::ivec2(1, 1);
        std::vector<uint32_t> StampTileIds; // Row-major, size = StampSize.x * StampSize.y.

        // Tile asset keys corresponding to each StampTileId / ActiveTileId.
        // Used to register tiles in the layer's TileTable when painting.
        std::string ActiveTileAssetKey;
        std::vector<std::string> StampTileAssetKeys; // Parallel to StampTileIds.

        bool HasStamp() const
        {
            return StampSize.x > 0 && StampSize.y > 0 &&
                   static_cast<int>(StampTileIds.size()) == StampSize.x * StampSize.y;
        }

        void ClearStamp()
        {
            StampSize = glm::ivec2(1, 1);
            StampTileIds.clear();
            StampTileIds.push_back(ActiveTileId);
            StampTileAssetKeys.clear();
            StampTileAssetKeys.push_back(ActiveTileAssetKey);
        }

        void SetSingleTile(uint32_t tileId, const std::string& tileAssetKey = {})
        {
            ActiveTileId = tileId;
            ActiveTileAssetKey = tileAssetKey;
            StampSize = glm::ivec2(1, 1);
            StampTileIds.clear();
            StampTileIds.push_back(tileId);
            StampTileAssetKeys.clear();
            StampTileAssetKeys.push_back(tileAssetKey);
        }
    };

    void Draw(uint32_t& sceneViewWidthPixels,
              uint32_t& sceneViewHeightPixels,
              std::shared_ptr<Framebuffer>& sceneViewFramebuffer,
              bool& sceneViewFocused,
              bool& sceneViewHovered,
              uint32_t& gameViewWidthPixels,
              uint32_t& gameViewHeightPixels,
              std::shared_ptr<Framebuffer>& gameViewFramebuffer,
              bool& gameViewFocused,
              bool& gameViewHovered,
              bool& focusSceneViewRequested,
              bool& focusGameViewRequested,
              EditorCameraController* editorCameraController,
              Camera* sceneViewCamera,
              Camera* gameViewCamera,
              Scene* scene,
              EditorPlayModeState playModeState,
              const std::function<void(uint32_t, uint32_t)>& ensureSceneViewFramebuffer,
              const std::function<void(uint32_t, uint32_t)>& ensureGameViewFramebuffer,
              const char* scenePayloadId,
              const std::function<void(const std::string&)>& onSceneDropped,
              const char* prefabPayloadId,
              const std::function<void(const std::string&, const glm::vec3&)>& onPrefabDropped,
              entt::entity& selectedEntity,
              EditorUndoService* undoService,
              const char* materialPayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              bool showFpsOverlay,
              TilemapEditorState* tilemapEditorState,
              bool showMissingGameplayCameraOverlay);
}
