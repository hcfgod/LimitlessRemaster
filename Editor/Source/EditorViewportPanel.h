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
        bool SnapToGrid = true;
        TilemapPaintMode PaintMode = TilemapPaintMode::Single;
        int32_t ActiveLayerIndex = 0;
        int32_t BrushSize = 1;
        uint32_t ActiveTileId = 1;
        bool PaintCustomData = false;
        uint32_t ActiveCustomData = 0;
        bool HasHoveredCell = false;
        glm::ivec2 HoveredCell = glm::ivec2(0, 0);
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
