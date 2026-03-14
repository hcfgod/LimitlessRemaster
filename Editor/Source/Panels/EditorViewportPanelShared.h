#pragma once

#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"

#include "imgui/imgui.h"

#include <optional>

namespace Limitless::EditorViewportPanel::Internal
{
    struct ViewportPanelContext
    {
        uint32_t& SceneViewWidthPixels;
        uint32_t& SceneViewHeightPixels;
        std::shared_ptr<Framebuffer>& SceneViewFramebuffer;
        bool& ShowSceneView;
        bool& SceneViewFocused;
        bool& SceneViewHovered;
        bool& SceneViewRectValid;
        glm::vec2& SceneViewRectMinPixels;
        glm::vec2& SceneViewRectMaxPixels;
        uint32_t& GameViewWidthPixels;
        uint32_t& GameViewHeightPixels;
        std::shared_ptr<Framebuffer>& GameViewFramebuffer;
        bool& ShowGameView;
        bool& GameViewFocused;
        bool& GameViewHovered;
        bool& GameViewRectValid;
        glm::vec2& GameViewRectMinPixels;
        glm::vec2& GameViewRectMaxPixels;
        bool& FocusSceneViewRequested;
        bool& FocusGameViewRequested;
        Camera* SceneViewCamera;
        Camera* GameViewCamera;
        Scene*& SceneContext;
        const std::function<void(Camera&, const std::shared_ptr<Framebuffer>&, uint32_t, uint32_t)>& RenderGameView;
        EditorPlayModeState PlayModeState;
        const std::function<void(uint32_t, uint32_t)>& EnsureSceneViewFramebuffer;
        const std::function<void(uint32_t, uint32_t)>& EnsureGameViewFramebuffer;
        const char* ScenePayloadId;
        const std::function<void(const std::string&)>& OnSceneDropped;
        const char* PrefabPayloadId;
        const std::function<void(const std::string&, const glm::vec3&)>& OnPrefabDropped;
        entt::entity& SelectedEntity;
        EditorUndoService* UndoService;
        const char* AssetMovePayloadId;
        const char* MaterialPayloadId;
        std::string& SelectedTextureAssetKey;
        Assets::TextureAsset::Ptr& CachedTextureAsset;
        std::string& SelectedMaterialAssetKey;
        Assets::MaterialAsset::Ptr& CachedMaterialAsset;
        std::string& SelectedNativeScriptAssetKey;
        bool ShowFpsOverlay;
        TilemapEditorState* TilemapState;
        bool ShowMissingGameplayCameraOverlay;
        TransformGizmoState* GizmoState;
        EditorScenePanelState* ScenePanelState;
        bool ShowGizmoToolbar;
        std::string& PendingDroppedSceneAssetKey;
    };

    uint32_t SanitizeViewportDimension(float value);
    bool DrawLoadingOverlay(Scene* scene, const ImVec2& minPos, const ImVec2& maxPos);
    void DrawSceneViewWindow(ViewportPanelContext& context);
    void DrawGameViewWindow(ViewportPanelContext& context);

    std::string NormalizeSlashes(std::string pathText);
    std::string ToLowerAscii(std::string text);

    bool TryComputeDropWorldPosition(const Camera& camera,
                                     const ImVec2& viewportMin,
                                     const ImVec2& viewportMax,
                                     const ImVec2& mouseScreenPosition,
                                     glm::vec3& outWorldPosition);
    bool TryComputeViewportRay(const Camera& camera,
                               const ImVec2& viewportMin,
                               const ImVec2& viewportMax,
                               const ImVec2& mouseScreenPosition,
                               glm::vec3& outRayOrigin,
                               glm::vec3& outRayDirection);
    bool ProjectLineSegmentClipped(const Camera& camera,
                                   const ImVec2& viewportMin,
                                   float viewportWidth,
                                   float viewportHeight,
                                   const glm::vec3& worldA,
                                   const glm::vec3& worldB,
                                   ImVec2& outScreenA,
                                   ImVec2& outScreenB);
    bool TryIntersectRayWithPlane(const glm::vec3& rayOrigin,
                                  const glm::vec3& rayDirection,
                                  const glm::vec3& planePoint,
                                  const glm::vec3& planeNormal,
                                  glm::vec3& outIntersectionPoint);
    bool IsEntityUnderCanvas(Scene& scene, entt::entity entity);
    std::optional<entt::entity> PickTopmostSpriteEntityAtPoint(Scene& scene,
                                                               const Camera& camera,
                                                               const ImVec2& viewportMin,
                                                               float viewportWidth,
                                                               float viewportHeight,
                                                               const ImVec2& mouseScreenPosition);
    bool WorldToViewportPoint(const Camera& camera,
                              const ImVec2& viewportMin,
                              float viewportWidth,
                              float viewportHeight,
                              const glm::vec3& worldPoint,
                              ImVec2& outPoint);
    bool IsMouseNearPoint(const ImVec2& mousePosition, const ImVec2& point, float radiusPixels);
    float DistanceToLineSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b);

    void HandleSceneViewDragDrop(ViewportPanelContext& context, const ImVec2& viewportMin, const ImVec2& viewportMax);

    glm::vec2 GetGrid2DFirstCellCenter(const Grid2DComponent& grid, const TilemapLayerComponent& layer);
    bool TryGetGrid2DCellUnderCursor(const Camera& camera,
                                     const glm::mat4& worldTransform,
                                     const Grid2DComponent& grid,
                                     const TilemapLayerComponent& layer,
                                     const ImVec2& viewportMin,
                                     const ImVec2& viewportMax,
                                     const ImVec2& mousePosition,
                                     glm::ivec2& outCell);
    bool DrawAndHandleGrid2DEditing(ImDrawList* drawList,
                                    Scene& scene,
                                    const Camera& camera,
                                    entt::entity gridEntity,
                                    entt::entity layerEntity,
                                    const ImVec2& viewportMin,
                                    const ImVec2& viewportMax,
                                    float viewportWidth,
                                    float viewportHeight,
                                    EditorPlayModeState playModeState,
                                    EditorUndoService* undoService,
                                    TilemapEditorState& tilemapEditorState,
                                    const std::string& activePaletteKey);

    void DrawSelectionHighlight(ImDrawList* drawList,
                                Scene& scene,
                                const Camera& camera,
                                entt::entity entity,
                                const ImVec2& viewportMin,
                                float viewportWidth,
                                float viewportHeight,
                                ImU32 color);
    void HandleSceneViewPicking(Scene& scene,
                                const Camera& camera,
                                entt::entity& selectedEntity,
                                EditorScenePanelState* scenePanelState,
                                const ImVec2& viewportMin,
                                const ImVec2& viewportMax,
                                float viewportWidth,
                                float viewportHeight,
                                bool sceneViewHovered,
                                TransformGizmoState* gizmoState);
    void HandleBoxSelection(ImDrawList* drawList,
                            Scene& scene,
                            const Camera& camera,
                            entt::entity& selectedEntity,
                            EditorScenePanelState* scenePanelState,
                            const ImVec2& viewportMin,
                            const ImVec2& viewportMax,
                            float viewportWidth,
                            float viewportHeight,
                            bool sceneViewHovered,
                            TransformGizmoState* gizmoState);

    bool DrawAndHandleTransformGizmos(ImDrawList* drawList,
                                      Scene& scene,
                                      const Camera& camera,
                                      entt::entity selectedEntity,
                                      const std::vector<entt::entity>& multiSelectedEntities,
                                      const ImVec2& viewportMin,
                                      const ImVec2& viewportMax,
                                      float viewportWidth,
                                      float viewportHeight,
                                      EditorPlayModeState playModeState,
                                      EditorUndoService* undoService,
                                      TransformGizmoState& gizmoState);
    void HandleGizmoKeyboardShortcuts(TransformGizmoState& gizmoState, bool viewportFocused);
    void DrawGizmoToolbar(ImDrawList* drawList,
                          const ImVec2& viewportMin,
                          const ImVec2& viewportMax,
                          TransformGizmoState& gizmoState);

    bool DrawSelectedPhysicsOverlays(ImDrawList* drawList,
                                     Scene& scene,
                                     const Camera& camera,
                                     entt::entity selectedEntity,
                                     const ImVec2& viewportMin,
                                     const ImVec2& viewportMax,
                                     float viewportWidth,
                                     float viewportHeight,
                                     EditorPlayModeState playModeState,
                                     EditorUndoService* undoService);
}
