#include "EditorViewportPanelShared.h"

#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>

namespace Limitless::EditorViewportPanel::Internal
{
    // -----------------------------------------------------------------
    // Selection highlight (wireframe outline for selected entities)
    // -----------------------------------------------------------------

    void DrawSelectionHighlight(ImDrawList* drawList,
                                Scene& scene,
                                const Camera& camera,
                                entt::entity entity,
                                const ImVec2& viewportMin,
                                float viewportWidth,
                                float viewportHeight,
                                ImU32 color)
    {
        if (!scene.IsValid(entity))
            return;
        auto& registry = scene.GetRegistry();
        if (!registry.try_get<TransformComponent>(entity))
            return;

        const glm::mat4 model = scene.GetWorldTransformMatrix(entity);
        std::array<ImVec2, 4> projected{};
        bool valid = true;
        for (size_t i = 0; i < 4; ++i)
        {
            const glm::vec4 world = model * glm::vec4((i == 1 || i == 2) ? 0.5f : -0.5f,
                                                      (i >= 2) ? 0.5f : -0.5f,
                                                      0.0f,
                                                      1.0f);
            if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(world), projected[i]))
            {
                valid = false;
                break;
            }
        }
        if (!valid)
            return;

        for (int i = 0; i < 4; ++i)
            drawList->AddLine(projected[i], projected[(i + 1) % 4], color, 1.5f);
    }

    // -----------------------------------------------------------------
    // Scene view entity picking and multi-selection
    // -----------------------------------------------------------------

    void HandleSceneViewPicking(Scene& scene,
                                const Camera& camera,
                                entt::entity& selectedEntity,
                                EditorScenePanelState* scenePanelState,
                                const ImVec2& viewportMin,
                                const ImVec2& viewportMax,
                                float viewportWidth,
                                float viewportHeight,
                                bool sceneViewHovered,
                                TransformGizmoState* gizmoState)
    {
        if (!sceneViewHovered)
            return;
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;
        if (gizmoState && gizmoState->DragActive)
            return;

        const ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x < viewportMin.x || mousePos.x > viewportMax.x ||
            mousePos.y < viewportMin.y || mousePos.y > viewportMax.y)
            return;

        if (ImGui::GetIO().MouseDown[ImGuiMouseButton_Right])
            return;

        const auto picked = PickTopmostSpriteEntityAtPoint(
            scene, camera, viewportMin, viewportWidth, viewportHeight, mousePos);

        const ImGuiIO& io = ImGui::GetIO();
        const bool ctrlHeld = io.KeyCtrl || io.KeySuper;

        if (picked.has_value())
        {
            const entt::entity pickedEntity = *picked;

            if (ctrlHeld && scenePanelState)
            {
                auto& multi = scenePanelState->MultiSelectedEntities;
                auto it = std::find(multi.begin(), multi.end(), pickedEntity);
                if (it != multi.end())
                {
                    multi.erase(it);
                    if (selectedEntity == pickedEntity)
                        selectedEntity = multi.empty() ? entt::null : multi.back();
                }
                else
                {
                    multi.push_back(pickedEntity);
                    selectedEntity = pickedEntity;
                }
                scenePanelState->SelectionAnchorEntity = pickedEntity;
            }
            else
            {
                selectedEntity = pickedEntity;
                if (scenePanelState)
                {
                    scenePanelState->MultiSelectedEntities.clear();
                    scenePanelState->MultiSelectedEntities.push_back(pickedEntity);
                    scenePanelState->SelectionAnchorEntity = pickedEntity;
                }
            }
        }
        else if (!ctrlHeld)
        {
            selectedEntity = entt::null;
            if (scenePanelState)
            {
                scenePanelState->MultiSelectedEntities.clear();
                scenePanelState->SelectionAnchorEntity = entt::null;
            }
        }
    }

    // -----------------------------------------------------------------
    // Box (marquee) selection
    // -----------------------------------------------------------------

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
                            TransformGizmoState* gizmoState)
    {
        if (!gizmoState || !scenePanelState)
            return;

        if (gizmoState->DragActive)
            return;
        if (ImGui::GetIO().MouseDown[ImGuiMouseButton_Right])
        {
            gizmoState->BoxSelectActive = false;
            return;
        }

        const ImVec2 mousePos = ImGui::GetMousePos();

        if (sceneViewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !gizmoState->DragActive)
        {
            gizmoState->BoxSelectStart = glm::vec2(mousePos.x, mousePos.y);
        }

        if (!gizmoState->BoxSelectActive && sceneViewHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const float dx = mousePos.x - gizmoState->BoxSelectStart.x;
            const float dy = mousePos.y - gizmoState->BoxSelectStart.y;
            if (dx * dx + dy * dy > 25.0f)
                gizmoState->BoxSelectActive = true;
        }

        if (gizmoState->BoxSelectActive)
        {
            const ImVec2 boxMin(std::min(gizmoState->BoxSelectStart.x, mousePos.x),
                                std::min(gizmoState->BoxSelectStart.y, mousePos.y));
            const ImVec2 boxMax(std::max(gizmoState->BoxSelectStart.x, mousePos.x),
                                std::max(gizmoState->BoxSelectStart.y, mousePos.y));

            drawList->AddRect(boxMin, boxMax, IM_COL32(100, 180, 255, 220), 0.0f, 0, 1.5f);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(100, 180, 255, 40));

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                gizmoState->BoxSelectActive = false;

                auto& registry = scene.GetRegistry();
                auto view = registry.view<TransformComponent, SpriteComponent>();

                const ImGuiIO& io = ImGui::GetIO();
                const bool ctrlHeld = io.KeyCtrl || io.KeySuper;

                if (!ctrlHeld)
                {
                    scenePanelState->MultiSelectedEntities.clear();
                    selectedEntity = entt::null;
                }

                for (entt::entity entity : view)
                {
                    if (IsEntityUnderCanvas(scene, entity))
                        continue;

                    const glm::mat4 model = scene.GetWorldTransformMatrix(entity);
                    const glm::vec3 worldPos = glm::vec3(model[3]);
                    ImVec2 screenPos{};
                    if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldPos, screenPos))
                        continue;

                    if (screenPos.x >= boxMin.x && screenPos.x <= boxMax.x &&
                        screenPos.y >= boxMin.y && screenPos.y <= boxMax.y)
                    {
                        auto& multi = scenePanelState->MultiSelectedEntities;
                        if (std::find(multi.begin(), multi.end(), entity) == multi.end())
                            multi.push_back(entity);
                        selectedEntity = entity;
                    }
                }

                if (!scenePanelState->MultiSelectedEntities.empty())
                {
                    selectedEntity = scenePanelState->MultiSelectedEntities.back();
                    scenePanelState->SelectionAnchorEntity = selectedEntity;
                }
            }
        }

        if (gizmoState->BoxSelectActive && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            gizmoState->BoxSelectActive = false;
    }
}
