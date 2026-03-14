#include "EditorViewportPanelShared.h"

#include "Editor/EditorCameraController.h"
#include "Scene/SceneRenderer.h"

#include <cmath>
#include <string>

namespace Limitless::EditorViewportPanel::Internal
{
    uint32_t SanitizeViewportDimension(float value)
    {
        if (!std::isfinite(value) || value <= 1.0f)
            return 0;
        return static_cast<uint32_t>(std::floor(value));
    }
}

namespace Limitless::EditorViewportPanel
{
    void Draw(uint32_t& sceneViewWidthPixels,
              uint32_t& sceneViewHeightPixels,
              std::shared_ptr<Framebuffer>& sceneViewFramebuffer,
              bool& showSceneView,
              bool& sceneViewFocused,
              bool& sceneViewHovered,
              bool& sceneViewRectValid,
              glm::vec2& sceneViewRectMinPixels,
              glm::vec2& sceneViewRectMaxPixels,
              uint32_t& gameViewWidthPixels,
              uint32_t& gameViewHeightPixels,
              std::shared_ptr<Framebuffer>& gameViewFramebuffer,
              bool& showGameView,
              bool& gameViewFocused,
              bool& gameViewHovered,
              bool& gameViewRectValid,
              glm::vec2& gameViewRectMinPixels,
              glm::vec2& gameViewRectMaxPixels,
              bool& focusSceneViewRequested,
              bool& focusGameViewRequested,
              EditorCameraController* editorCameraController,
              Camera* sceneViewCamera,
              Camera* gameViewCamera,
              Scene* scene,
              const std::function<void(Camera&, const std::shared_ptr<Framebuffer>&, uint32_t, uint32_t)>& renderGameView,
              EditorPlayModeState playModeState,
              const std::function<void(uint32_t, uint32_t)>& ensureSceneViewFramebuffer,
              const std::function<void(uint32_t, uint32_t)>& ensureGameViewFramebuffer,
              const char* scenePayloadId,
              const std::function<void(const std::string&)>& onSceneDropped,
              const char* prefabPayloadId,
              const std::function<void(const std::string&, const glm::vec3&)>& onPrefabDropped,
              entt::entity& selectedEntity,
              EditorUndoService* undoService,
              const char* assetMovePayloadId,
              const char* materialPayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              bool showFpsOverlay,
              TilemapEditorState* tilemapEditorState,
              bool showMissingGameplayCameraOverlay,
              TransformGizmoState* gizmoState,
              EditorScenePanelState* scenePanelState,
              bool showGizmoToolbar)
    {
        (void)editorCameraController;
        sceneViewFocused = false;
        sceneViewHovered = false;
        sceneViewRectValid = false;
        sceneViewRectMinPixels = glm::vec2(0.0f);
        sceneViewRectMaxPixels = glm::vec2(0.0f);
        gameViewFocused = false;
        gameViewHovered = false;
        gameViewRectValid = false;
        gameViewRectMinPixels = glm::vec2(0.0f);
        gameViewRectMaxPixels = glm::vec2(0.0f);

        std::string pendingDroppedSceneAssetKey;
        Internal::ViewportPanelContext context{
            sceneViewWidthPixels,
            sceneViewHeightPixels,
            sceneViewFramebuffer,
            showSceneView,
            sceneViewFocused,
            sceneViewHovered,
            sceneViewRectValid,
            sceneViewRectMinPixels,
            sceneViewRectMaxPixels,
            gameViewWidthPixels,
            gameViewHeightPixels,
            gameViewFramebuffer,
            showGameView,
            gameViewFocused,
            gameViewHovered,
            gameViewRectValid,
            gameViewRectMinPixels,
            gameViewRectMaxPixels,
            focusSceneViewRequested,
            focusGameViewRequested,
            sceneViewCamera,
            gameViewCamera,
            scene,
            renderGameView,
            playModeState,
            ensureSceneViewFramebuffer,
            ensureGameViewFramebuffer,
            scenePayloadId,
            onSceneDropped,
            prefabPayloadId,
            onPrefabDropped,
            selectedEntity,
            undoService,
            assetMovePayloadId,
            materialPayloadId,
            selectedTextureAssetKey,
            cachedTextureAsset,
            selectedMaterialAssetKey,
            cachedMaterialAsset,
            selectedNativeScriptAssetKey,
            showFpsOverlay,
            tilemapEditorState,
            showMissingGameplayCameraOverlay,
            gizmoState,
            scenePanelState,
            showGizmoToolbar,
            pendingDroppedSceneAssetKey
        };

        Internal::DrawSceneViewWindow(context);
        SceneRenderer::SetUiInputViewportRectPixels(0.0f, 0.0f, 0.0f, 0.0f, false);
        Internal::DrawGameViewWindow(context);
    }
}
