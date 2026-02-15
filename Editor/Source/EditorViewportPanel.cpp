#include "EditorViewportPanel.h"

#include "Assets/AssetLoadProgress.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Renderer2D.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

namespace Limitless::EditorViewportPanel
{
    namespace
    {
        // Renderer2D quad vertices in local space (must match Renderer2D::DrawQuad).
        constexpr std::array<glm::vec4, 4> kQuadLocalPositions = {
            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4(0.5f, 0.5f, 0.0f, 1.0f),
            glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f),
        };

        float Sign2D(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
        {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        }

        bool PointInTriangle(const ImVec2& point, const ImVec2& a, const ImVec2& b, const ImVec2& c)
        {
            const float d1 = Sign2D(point, a, b);
            const float d2 = Sign2D(point, b, c);
            const float d3 = Sign2D(point, c, a);

            const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
            const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
            return !(hasNeg && hasPos);
        }

        bool PointInProjectedQuad(const ImVec2& point, const std::array<ImVec2, 4>& quad)
        {
            // Quad is convex; treat as two triangles (0,1,2) and (2,3,0).
            return PointInTriangle(point, quad[0], quad[1], quad[2]) || PointInTriangle(point, quad[2], quad[3], quad[0]);
        }

        bool TryComputeDropWorldPosition(const Camera& camera,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         const ImVec2& mouseScreenPosition,
                                         glm::vec3& outWorldPosition)
        {
            const float viewportWidth = viewportMax.x - viewportMin.x;
            const float viewportHeight = viewportMax.y - viewportMin.y;
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const float normalizedX = (mouseScreenPosition.x - viewportMin.x) / viewportWidth;
            const float normalizedY = (mouseScreenPosition.y - viewportMin.y) / viewportHeight;
            const float ndcX = normalizedX * 2.0f - 1.0f;
            const float ndcY = 1.0f - normalizedY * 2.0f;

            const glm::mat4 inverseViewProjection = glm::inverse(camera.GetViewProjectionMatrix());
            if (camera.GetType() == CameraType::Orthographic2D)
            {
                const glm::vec4 world = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                if (world.w == 0.0f)
                    return false;
                outWorldPosition = glm::vec3(world) / world.w;
                outWorldPosition.z = 0.0f;
                return true;
            }

            const glm::vec4 nearWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            const glm::vec4 farWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            if (nearWorldH.w == 0.0f || farWorldH.w == 0.0f)
                return false;

            const glm::vec3 nearWorld = glm::vec3(nearWorldH) / nearWorldH.w;
            const glm::vec3 farWorld = glm::vec3(farWorldH) / farWorldH.w;
            const glm::vec3 direction = glm::normalize(farWorld - nearWorld);

            // Place prefab where the ray intersects the world Z=0 plane.
            constexpr float kTargetPlaneZ = 0.0f;
            if (std::abs(direction.z) > 0.0001f)
            {
                const float distance = (kTargetPlaneZ - nearWorld.z) / direction.z;
                outWorldPosition = nearWorld + direction * distance;
            }
            else
            {
                outWorldPosition = nearWorld + direction * 5.0f;
                outWorldPosition.z = kTargetPlaneZ;
            }

            return true;
        }

        std::optional<entt::entity> PickTopmostSpriteEntityAtPoint(Scene& scene,
                                                                   const Camera& camera,
                                                                   const ImVec2& viewportMin,
                                                                   float viewportWidth,
                                                                   float viewportHeight,
                                                                   const ImVec2& mouseScreenPosition)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<TransformComponent, SpriteComponent>();
            if (view.begin() == view.end())
                return std::nullopt;

            const glm::mat4& viewProjection = camera.GetViewProjectionMatrix();
            entt::entity bestEntity = entt::null;
            float bestWorldZ = -std::numeric_limits<float>::infinity();
            int32_t bestSiblingOrder = std::numeric_limits<int32_t>::min();

            for (entt::entity entity : view)
            {
                const glm::mat4 model = scene.GetWorldTransformMatrix(entity);

                std::array<ImVec2, 4> projected{};
                bool anyBehindCamera = false;
                for (size_t i = 0; i < projected.size(); ++i)
                {
                    const glm::vec4 world = model * kQuadLocalPositions[i];
                    const glm::vec4 clip = viewProjection * world;
                    if (clip.w == 0.0f)
                    {
                        anyBehindCamera = true;
                        break;
                    }

                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    // Outside clip volume is fine; the point-in-quad test will fail naturally.
                    const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
                    const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
                    projected[i] = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
                }

                if (anyBehindCamera)
                    continue;

                if (!PointInProjectedQuad(mouseScreenPosition, projected))
                    continue;

                const glm::mat4 worldTransform = model;
                const float worldZ = worldTransform[3].z;
                const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
                const int32_t siblingOrder = hierarchy ? hierarchy->SiblingOrder : 0;

                // Topmost = rendered last. Render sort uses increasing Z and increasing sibling order.
                if (worldZ > bestWorldZ || (worldZ == bestWorldZ && siblingOrder > bestSiblingOrder))
                {
                    bestWorldZ = worldZ;
                    bestSiblingOrder = siblingOrder;
                    bestEntity = entity;
                }
            }

            if (bestEntity == entt::null)
                return std::nullopt;
            return bestEntity;
        }

        bool WorldToViewportPoint(const Camera& camera,
                                  const ImVec2& viewportMin,
                                  float viewportWidth,
                                  float viewportHeight,
                                  const glm::vec3& worldPoint,
                                  ImVec2& outPoint)
        {
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const glm::vec4 clip = camera.GetViewProjectionMatrix() * glm::vec4(worldPoint, 1.0f);
            if (std::abs(clip.w) <= 0.000001f)
                return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
            const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
            outPoint = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
            return true;
        }

        void DrawSelectedPhysicsOverlays(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         float viewportWidth,
                                         float viewportHeight)
        {
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return;

            auto& registry = scene.GetRegistry();
            auto* transform = registry.try_get<TransformComponent>(selectedEntity);
            if (!transform)
                return;

            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);

            if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(selectedEntity))
            {
                const glm::vec3 localCorners[4] = {
                    glm::vec3(boxCollider2D->Offset.x - boxCollider2D->Size.x * 0.5f, boxCollider2D->Offset.y - boxCollider2D->Size.y * 0.5f, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + boxCollider2D->Size.x * 0.5f, boxCollider2D->Offset.y - boxCollider2D->Size.y * 0.5f, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + boxCollider2D->Size.x * 0.5f, boxCollider2D->Offset.y + boxCollider2D->Size.y * 0.5f, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x - boxCollider2D->Size.x * 0.5f, boxCollider2D->Offset.y + boxCollider2D->Size.y * 0.5f, 0.0f)
                };

                ImVec2 projectedCorners[4]{};
                bool valid = true;
                for (int i = 0; i < 4; ++i)
                {
                    const glm::vec4 worldCorner = worldTransform * glm::vec4(localCorners[i], 1.0f);
                    if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCorner), projectedCorners[i]))
                    {
                        valid = false;
                        break;
                    }
                }

                if (valid)
                {
                    drawList->AddLine(projectedCorners[0], projectedCorners[1], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[1], projectedCorners[2], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[2], projectedCorners[3], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[3], projectedCorners[0], IM_COL32(90, 200, 255, 255), 2.0f);
                }
            }

            if (auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(selectedEntity))
            {
                const glm::vec4 worldCenter = worldTransform * glm::vec4(circleCollider2D->Offset.x, circleCollider2D->Offset.y, 0.0f, 1.0f);
                const glm::vec4 worldRadiusPoint = worldTransform * glm::vec4(circleCollider2D->Offset.x + circleCollider2D->Radius, circleCollider2D->Offset.y, 0.0f, 1.0f);

                ImVec2 centerPoint{};
                ImVec2 radiusPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCenter), centerPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldRadiusPoint), radiusPoint))
                {
                    const float radiusPixels = std::sqrt((radiusPoint.x - centerPoint.x) * (radiusPoint.x - centerPoint.x) +
                                                         (radiusPoint.y - centerPoint.y) * (radiusPoint.y - centerPoint.y));
                    drawList->AddCircle(centerPoint, radiusPixels, IM_COL32(255, 190, 70, 255), 48, 2.0f);
                }
            }

            if (auto* joint2D = registry.try_get<Joint2DComponent>(selectedEntity))
            {
                if (joint2D->ConnectedEntity != entt::null && scene.IsValid(joint2D->ConnectedEntity))
                {
                    const glm::mat4 connectedWorldTransform = scene.GetWorldTransformMatrix(joint2D->ConnectedEntity);
                    const glm::vec4 worldAnchorA = worldTransform * glm::vec4(joint2D->AnchorA, 0.0f, 1.0f);
                    const glm::vec4 worldAnchorB = connectedWorldTransform * glm::vec4(joint2D->AnchorB, 0.0f, 1.0f);

                    ImVec2 projectedA{};
                    ImVec2 projectedB{};
                    if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldAnchorA), projectedA) &&
                        WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldAnchorB), projectedB))
                    {
                        drawList->AddLine(projectedA, projectedB, IM_COL32(130, 255, 130, 255), 2.0f);
                        drawList->AddCircleFilled(projectedA, 4.0f, IM_COL32(130, 255, 130, 255));
                        drawList->AddCircleFilled(projectedB, 4.0f, IM_COL32(130, 255, 130, 255));
                    }
                }
            }
        }
    }

    void Draw(uint32_t& viewportWidthPixels,
              uint32_t& viewportHeightPixels,
              std::shared_ptr<Framebuffer>& viewportFramebuffer,
              bool& viewportFocused,
              bool& viewportHovered,
              EditorCameraController* editorCameraController,
              CameraManager& cameraManager,
              Scene* scene,
              EditorPlayModeState playModeState,
              bool playModeMissingGameplayCamera,
              const std::function<void(uint32_t, uint32_t)>& ensureViewportFramebuffer,
              const char* scenePayloadId,
              const std::function<void(const std::string&)>& onSceneDropped,
              const char* prefabPayloadId,
              const std::function<void(const std::string&, const glm::vec3&)>& onPrefabDropped,
              entt::entity& selectedEntity,
              const char* materialPayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        viewportFocused = ImGui::IsWindowFocused();
        viewportHovered = ImGui::IsWindowHovered();
        const bool skipRender = ImGui::IsWindowCollapsed();

        const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        const uint32_t width = static_cast<uint32_t>(viewportSize.x);
        const uint32_t height = static_cast<uint32_t>(viewportSize.y);

        if (!skipRender && width > 0 && height > 0)
        {
            ensureViewportFramebuffer(width, height);

            if (viewportWidthPixels != width || viewportHeightPixels != height)
            {
                viewportWidthPixels = width;
                viewportHeightPixels = height;
                if (editorCameraController)
                    editorCameraController->OnWindowResize(width, height);
            }

            Camera* camera = cameraManager.GetActiveCamera();
            if (camera)
                camera->SetViewportSize(width, height);
            if (camera && scene && viewportFramebuffer)
                SceneRenderer::RenderToViewport(*scene, *camera, viewportFramebuffer, width, height);

            if (viewportFramebuffer && viewportFramebuffer->GetColorAttachment())
            {
                ImGui::Image(
                    (ImTextureID)(void*)(uintptr_t)viewportFramebuffer->GetColorAttachment()->GetRendererID(),
                    ImVec2(static_cast<float>(width), static_cast<float>(height)),
                    ImVec2(0, 1),
                    ImVec2(1, 0));

                if (scene && camera)
                {
                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    DrawSelectedPhysicsOverlays(drawList, *scene, *camera, selectedEntity, viewportMin, static_cast<float>(width), static_cast<float>(height));
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(scenePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0] && onSceneDropped)
                            onSceneDropped(key);
                    }
                    if (prefabPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0] && onPrefabDropped)
                            {
                                glm::vec3 worldPosition(0.0f);
                                if (camera)
                                {
                                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                    const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                    const ImVec2 mousePos = ImGui::GetMousePos();
                                    if (!TryComputeDropWorldPosition(*camera, viewportMin, viewportMax, mousePos, worldPosition))
                                        worldPosition = glm::vec3(0.0f);
                                }
                                onPrefabDropped(key, worldPosition);
                            }
                        }
                    }
                    if (materialPayloadId)
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0] && scene && camera)
                            {
                                // Unity-style: dropping a material onto the Scene viewport assigns it to the hovered renderer.
                                // We do a simple CPU pick against projected sprite quads.
                                const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                const ImVec2 mousePos = ImGui::GetMousePos();

                                entt::entity targetEntity = entt::null;
                                if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                    mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                                {
                                    const float viewportWidth = viewportMax.x - viewportMin.x;
                                    const float viewportHeight = viewportMax.y - viewportMin.y;
                                    const auto picked = PickTopmostSpriteEntityAtPoint(*scene, *camera, viewportMin, viewportWidth, viewportHeight, mousePos);
                                    if (picked.has_value())
                                        targetEntity = *picked;
                                }

                                // Fallback: if we didn't hit anything, apply to current selection.
                                if (targetEntity == entt::null && selectedEntity != entt::null && scene->IsValid(selectedEntity))
                                    targetEntity = selectedEntity;

                                if (targetEntity != entt::null && scene->IsValid(targetEntity))
                                {
                                    auto& registry = scene->GetRegistry();
                                    if (registry.all_of<SpriteComponent>(targetEntity))
                                    {
                                        auto* material = registry.try_get<MaterialComponent>(targetEntity);
                                        if (!material)
                                            material = &registry.emplace<MaterialComponent>(targetEntity);

                                        material->MaterialKey = key;
                                        material->CachedMaterial.reset();
                                        material->MaterialLoadAttempted = false;

                                        selectedEntity = targetEntity;

                                        // Keep the Inspector focused on the object selection.
                                        selectedTextureAssetKey.clear();
                                        cachedTextureAsset.reset();
                                        selectedMaterialAssetKey.clear();
                                        cachedMaterialAsset.reset();
                                        selectedNativeScriptAssetKey.clear();
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!Renderer2D::IsShaderReady())
                {
                    const ImVec2 minPos = ImGui::GetItemRectMin();
                    const ImVec2 maxPos = ImGui::GetItemRectMax();
                    const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 160));

                    const char* loadingText = "Loading shader...";
                    float progressValue = 0.0f;
                    const auto progressInfo = Assets::AssetLoadProgress::GetProgress(Renderer2D::GetDefaultShaderKey());
                    if (progressInfo.has_value())
                    {
                        loadingText = progressInfo->Status.empty() ? "Loading shader..." : progressInfo->Status.c_str();
                        progressValue = progressInfo->Progress;
                    }
                    else
                    {
                        progressValue = std::fmod(static_cast<float>(ImGui::GetTime() * 0.8), 1.0f);
                    }

                    const ImVec2 textSize = ImGui::CalcTextSize(loadingText);
                    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f - 24.0f),
                                      IM_COL32(255, 255, 255, 255),
                                      loadingText);

                    const float barWidth = 200.0f;
                    const float barHeight = 8.0f;
                    const ImVec2 barMin(center.x - barWidth * 0.5f, center.y - barHeight * 0.5f + 8.0f);
                    const ImVec2 barMax(center.x + barWidth * 0.5f, center.y + barHeight * 0.5f + 8.0f);
                    drawList->AddRectFilled(barMin, barMax, IM_COL32(50, 50, 55, 255));
                    const ImVec2 fillMax(barMin.x + barWidth * progressValue, barMax.y);
                    drawList->AddRectFilled(barMin, fillMax, IM_COL32(80, 140, 220, 255));
                }
                else if (playModeState != EditorPlayModeState::Edit && playModeMissingGameplayCamera)
                {
                    const ImVec2 minPos = ImGui::GetItemRectMin();
                    const ImVec2 maxPos = ImGui::GetItemRectMax();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 140));

                    const char* text = "Play Mode: No active gameplay camera.\nAdd a Camera Component to an entity and set it as Primary.";
                    const ImVec2 textSize = ImGui::CalcTextSize(text);
                    const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
