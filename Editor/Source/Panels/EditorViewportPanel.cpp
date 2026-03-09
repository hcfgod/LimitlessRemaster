#include "EditorViewportPanel.h"

#include "Assets/AssetLoadProgress.h"
#include "Assets/LoadingScreen.h"
#include "Editor/EditorCameraController.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "EditorScenePanel.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/NativeRenderHandles.h"
#include "Graphics/Renderer2D.h"
#include "Core/Time.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"
#include "Scripting/ManagedScriptHost.h"
#include "Undo/EditorUndoService.h"
#include "imgui/imgui.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Limitless::EditorViewportPanel
{
    namespace
    {
        std::string NormalizeSlashes(std::string pathText)
        {
            std::replace(pathText.begin(), pathText.end(), '\\', '/');
            return pathText;
        }

        std::string ToLowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return text;
        }

        bool IsNativeScriptAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string lowerKey = ToLowerAscii(NormalizeSlashes(assetKey));
            return lowerKey.rfind("assets/", 0) == 0 &&
                   (lowerKey.ends_with(".h") || lowerKey.ends_with(".cpp"));
        }

        bool IsManagedScriptAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string lowerKey = ToLowerAscii(NormalizeSlashes(assetKey));
            return lowerKey.rfind("assets/", 0) == 0 && lowerKey.ends_with(".cs");
        }

        NativeScriptEntry BuildScriptEntryFromAssetKey(const std::string& assetKey)
        {
            NativeScriptEntry scriptEntry{};
            std::string relativePath = NormalizeSlashes(assetKey);
            if (relativePath.rfind("Assets/", 0) == 0)
                relativePath.erase(0, 7);

            std::filesystem::path scriptPath(relativePath);
            scriptPath.replace_extension();
            scriptEntry.ScriptAssetRelativePath = scriptPath.generic_string();

            const std::string requestedClassName = scriptPath.stem().string();
            const std::string resolvedClassName = EditorInspectorPanel::ResolveRegisteredScriptClassNameForInspector(requestedClassName);
            scriptEntry.ScriptClassName = resolvedClassName.empty() ? requestedClassName : resolvedClassName;
            return scriptEntry;
        }

        ManagedScriptEntry BuildManagedScriptEntryFromAssetKey(const std::string& assetKey)
        {
            ManagedScriptEntry scriptEntry{};
            std::string relativePath = NormalizeSlashes(assetKey);
            if (relativePath.rfind("Assets/", 0) == 0)
                relativePath.erase(0, 7);

            scriptEntry.ScriptAssetRelativePath = relativePath;
            const std::string requestedClassName = std::filesystem::path(relativePath).stem().string();
            const std::string resolvedClassName = ManagedScriptHost::ResolveDiscoveredClassName(requestedClassName);
            scriptEntry.ScriptClassName = resolvedClassName.empty() ? requestedClassName : resolvedClassName;
            return scriptEntry;
        }

        bool TryAttachScriptAssetToEntity(Scene* scene,
                                          entt::entity ownerEntity,
                                          const std::string& assetKey,
                                          EditorUndoService* undoService)
        {
            if (!scene || !scene->IsValid(ownerEntity))
                return false;

            if (IsNativeScriptAssetKey(assetKey))
            {
                NativeScriptEntry scriptEntry = BuildScriptEntryFromAssetKey(assetKey);
                if (scriptEntry.ScriptClassName.empty() && scriptEntry.ScriptAssetRelativePath.empty())
                    return false;

                if (undoService)
                {
                    return undoService->ExecuteSceneMutation("Attach Native Script", [&](Scene& mutableScene) {
                        return mutableScene.AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
                    });
                }

                return scene->AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
            }

            if (!IsManagedScriptAssetKey(assetKey))
                return false;

            ManagedScriptEntry scriptEntry = BuildManagedScriptEntryFromAssetKey(assetKey);
            if (scriptEntry.ScriptClassName.empty() && scriptEntry.ScriptAssetRelativePath.empty())
                return false;

            if (undoService)
            {
                return undoService->ExecuteSceneMutation("Attach Managed Script", [&](Scene& mutableScene) {
                    return mutableScene.AttachManagedScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
                });
            }

            return scene->AttachManagedScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
        }

        bool TryComputeDropWorldPosition(const Camera& camera,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         const ImVec2& mouseScreenPosition,
                                         glm::vec3& outWorldPosition);

        // ----- Grid2D undo/redo support ------------------------------------------

        struct Grid2DCellEdit
        {
            int32_t CellX = 0;
            int32_t CellY = 0;
            uint32_t PreviousTile = 0;
            uint32_t NewTile = 0;
        };

        class Grid2DPaintCommand final : public IEditorCommand
        {
        public:
            Grid2DPaintCommand(std::string label,
                               EditorUndoService* undoService,
                               entt::entity layerEntity,
                               std::vector<Grid2DCellEdit> edits)
                : m_Label(std::move(label)),
                  m_UndoService(undoService),
                  m_LayerEntity(layerEntity),
                  m_Edits(std::move(edits))
            {
            }

            bool Undo() override { return Apply(false); }
            bool Redo() override { return Apply(true); }
            const std::string& GetLabel() const override { return m_Label; }

        private:
            bool Apply(bool applyNewValues)
            {
                if (!m_UndoService)
                    return false;
                Scene* scene = m_UndoService->GetActiveScene();
                if (!scene || !scene->IsValid(m_LayerEntity))
                    return false;

                auto& registry = scene->GetRegistry();
                auto* layer = registry.try_get<TilemapLayerComponent>(m_LayerEntity);
                if (!layer)
                    return false;
                layer->EnsureStorage();

                for (const Grid2DCellEdit& edit : m_Edits)
                {
                    if (!IsLayerCellInBounds(*layer, edit.CellX, edit.CellY))
                        continue;
                    const size_t idx = LayerCellToIndex(*layer, edit.CellX, edit.CellY);
                    if (idx >= layer->Tiles.size())
                        continue;
                    layer->Tiles[idx] = applyNewValues ? edit.NewTile : edit.PreviousTile;
                }
                layer->RenderCacheDirty = true;
                return true;
            }

            std::string m_Label;
            EditorUndoService* m_UndoService = nullptr;
            entt::entity m_LayerEntity = entt::null;
            std::vector<Grid2DCellEdit> m_Edits;
        };

        struct Grid2DPaintDragState
        {
            bool Active = false;
            entt::entity LayerEntity = entt::null;
            std::unordered_map<size_t, Grid2DCellEdit> PendingEdits;
        };

        Grid2DPaintDragState& GetGrid2DPaintDragState()
        {
            static Grid2DPaintDragState state;
            return state;
        }

        /// Record a single cell edit for the Grid2D undo system. Writes the new
        /// value immediately and stores the old value for undo.
        void StageGrid2DEdit(TilemapLayerComponent& layer,
                             const glm::ivec2& cell,
                             uint32_t newTileValue,
                             Grid2DPaintDragState& dragState)
        {
            if (!IsLayerCellInBounds(layer, cell.x, cell.y))
                return;
            const size_t idx = LayerCellToIndex(layer, cell.x, cell.y);
            if (idx >= layer.Tiles.size())
                return;

            const uint32_t oldValue = layer.Tiles[idx];
            if (oldValue == newTileValue)
                return;

            // Only record the first change per cell within a single stroke.
            if (dragState.PendingEdits.find(idx) == dragState.PendingEdits.end())
            {
                Grid2DCellEdit edit;
                edit.CellX = cell.x;
                edit.CellY = cell.y;
                edit.PreviousTile = oldValue;
                edit.NewTile = newTileValue;
                dragState.PendingEdits.emplace(idx, edit);
            }
            else
            {
                dragState.PendingEdits[idx].NewTile = newTileValue;
            }

            layer.Tiles[idx] = newTileValue;
            layer.RenderCacheDirty = true;
        }

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

        bool TryComputeViewportRay(const Camera& camera,
                                   const ImVec2& viewportMin,
                                   const ImVec2& viewportMax,
                                   const ImVec2& mouseScreenPosition,
                                   glm::vec3& outRayOrigin,
                                   glm::vec3& outRayDirection)
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
            const glm::vec4 nearWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            const glm::vec4 farWorldH = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            if (std::abs(nearWorldH.w) <= 0.000001f || std::abs(farWorldH.w) <= 0.000001f)
                return false;

            const glm::vec3 nearWorld = glm::vec3(nearWorldH) / nearWorldH.w;
            const glm::vec3 farWorld = glm::vec3(farWorldH) / farWorldH.w;
            const glm::vec3 direction = farWorld - nearWorld;
            const float directionLength = glm::length(direction);
            if (directionLength <= 0.000001f)
                return false;

            outRayOrigin = nearWorld;
            outRayDirection = direction / directionLength;
            return true;
        }

        /// Project a world-space line segment to screen space with near-plane
        /// clipping. When one endpoint is behind the camera, the segment is
        /// clipped at the near plane so the visible portion still draws.
        bool ProjectLineSegmentClipped(const Camera& camera,
                                       const ImVec2& viewportMin,
                                       float viewportWidth,
                                       float viewportHeight,
                                       const glm::vec3& worldA,
                                       const glm::vec3& worldB,
                                       ImVec2& outScreenA,
                                       ImVec2& outScreenB)
        {
            if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
                return false;

            const glm::mat4& vp = camera.GetViewProjectionMatrix();
            glm::vec4 clipA = vp * glm::vec4(worldA, 1.0f);
            glm::vec4 clipB = vp * glm::vec4(worldB, 1.0f);

            constexpr float kNearEpsilon = 0.001f;
            const bool aInFront = clipA.w > kNearEpsilon;
            const bool bInFront = clipB.w > kNearEpsilon;

            if (!aInFront && !bInFront)
                return false;

            if (!aInFront || !bInFront)
            {
                const float t = (kNearEpsilon - clipA.w) / (clipB.w - clipA.w);
                const glm::vec4 clipped = clipA + std::clamp(t, 0.0f, 1.0f) * (clipB - clipA);
                if (!aInFront)
                    clipA = clipped;
                else
                    clipB = clipped;
            }

            auto clipToScreen = [&](const glm::vec4& clip, ImVec2& out) {
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                out.x = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportWidth;
                out.y = viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
            };

            clipToScreen(clipA, outScreenA);
            clipToScreen(clipB, outScreenB);
            return true;
        }

        bool TryIntersectRayWithPlane(const glm::vec3& rayOrigin,
                                      const glm::vec3& rayDirection,
                                      const glm::vec3& planePoint,
                                      const glm::vec3& planeNormal,
                                      glm::vec3& outIntersectionPoint)
        {
            const float denominator = glm::dot(rayDirection, planeNormal);
            if (std::abs(denominator) <= 0.000001f)
                return false;

            const float distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
            if (distance < 0.0f)
                return false;

            outIntersectionPoint = rayOrigin + rayDirection * distance;
            return true;
        }

        /// Returns true when the entity itself carries a CanvasComponent or is a
        /// descendant of an entity that does.  Screen-space UI should not be
        /// pickable in the scene view, matching Unity behaviour.
        bool IsEntityUnderCanvas(Scene& scene, entt::entity entity)
        {
            auto& registry = scene.GetRegistry();
            entt::entity current = entity;
            while (current != entt::null)
            {
                if (registry.any_of<CanvasComponent>(current))
                    return true;
                current = scene.GetParent(current);
            }
            return false;
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
                // Skip UI entities that live under a Canvas (screen-space UI).
                if (IsEntityUnderCanvas(scene, entity))
                    continue;

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
            // Prevent perspective back-projection artifacts when points move behind
            // the camera; these can make editor overlays appear to "warp" with
            // camera rotation.
            if (camera.GetType() == CameraType::Perspective3D && clip.w <= 0.0f)
                return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float pixelX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
            const float pixelY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight;
            outPoint = ImVec2(viewportMin.x + pixelX, viewportMin.y + pixelY);
            return true;
        }

        enum class ColliderHandleKind : uint8_t
        {
            None = 0,
            BoxOffset,
            BoxCorner0,
            BoxCorner1,
            BoxCorner2,
            BoxCorner3,
            CircleOffset,
            CircleRadius,
            PolygonOffset,
            PolygonPoint,
            EdgeOffset,
            EdgePointA,
            EdgePointB,
            CapsuleOffset,
            CapsuleCorner
        };

        struct ColliderDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            ColliderHandleKind Handle = ColliderHandleKind::None;
            const char* CommitLabel = nullptr;
            int PointIndex = -1;
        };

        ColliderDragState& GetColliderDragState()
        {
            static ColliderDragState state;
            return state;
        }

        enum class LightingHandleKind : uint8_t
        {
            None = 0,
            DirectionalDirection,
            PointRadius,
            OccluderPoint
        };

        struct LightingDragState final
        {
            bool Active = false;
            entt::entity Entity = entt::null;
            LightingHandleKind Handle = LightingHandleKind::None;
            int PointIndex = -1;
            const char* CommitLabel = nullptr;
        };

        LightingDragState& GetLightingDragState()
        {
            static LightingDragState state;
            return state;
        }

        bool IsMouseNearPoint(const ImVec2& mousePosition, const ImVec2& point, float radiusPixels)
        {
            const float dx = mousePosition.x - point.x;
            const float dy = mousePosition.y - point.y;
            return (dx * dx + dy * dy) <= radiusPixels * radiusPixels;
        }

        float DistanceToLineSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b)
        {
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float lengthSquared = abx * abx + aby * aby;
            if (lengthSquared <= 0.000001f)
                return std::sqrt((point.x - a.x) * (point.x - a.x) + (point.y - a.y) * (point.y - a.y));

            const float t = std::clamp(((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSquared, 0.0f, 1.0f);
            const float closestX = a.x + t * abx;
            const float closestY = a.y + t * aby;
            return std::sqrt((point.x - closestX) * (point.x - closestX) + (point.y - closestY) * (point.y - closestY));
        }

        glm::vec3 GetGizmoAxisDirection(int axis)
        {
            switch (axis)
            {
                case 0: return glm::vec3(1.0f, 0.0f, 0.0f);
                case 1: return glm::vec3(0.0f, 1.0f, 0.0f);
                default: return glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }

        glm::vec3 GetCameraForwardDirection(const Camera& camera)
        {
            const glm::mat4 inverseView = glm::inverse(camera.GetViewMatrix());
            const glm::vec3 forward = glm::vec3(inverseView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
            const float length = glm::length(forward);
            if (length <= 0.000001f)
                return glm::vec3(0.0f, 0.0f, -1.0f);
            return forward / length;
        }

        glm::vec3 ComputeAxisDragPlaneNormal(const Camera& camera, const glm::vec3& axisDirection, const glm::vec3& gizmoOrigin)
        {
            glm::vec3 viewDirection = -GetCameraForwardDirection(camera);
            if (camera.GetType() == CameraType::Perspective3D)
            {
                const glm::mat4 inverseView = glm::inverse(camera.GetViewMatrix());
                const glm::vec3 cameraPosition = glm::vec3(inverseView[3]);
                const glm::vec3 toCamera = cameraPosition - gizmoOrigin;
                const float toCameraLength = glm::length(toCamera);
                if (toCameraLength > 0.000001f)
                    viewDirection = toCamera / toCameraLength;
            }

            glm::vec3 planeNormal = viewDirection - axisDirection * glm::dot(viewDirection, axisDirection);
            const float planeNormalLength = glm::length(planeNormal);
            if (planeNormalLength > 0.000001f)
                return planeNormal / planeNormalLength;

            const glm::vec3 fallbackDirection = std::abs(axisDirection.z) < 0.999f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            planeNormal = fallbackDirection - axisDirection * glm::dot(fallbackDirection, axisDirection);
            const float fallbackLength = glm::length(planeNormal);
            if (fallbackLength <= 0.000001f)
                return glm::vec3(0.0f, 0.0f, 1.0f);
            return planeNormal / fallbackLength;
        }

        bool TryIntersectMouseWithPlane(const Camera& camera,
                                        const ImVec2& viewportMin,
                                        const ImVec2& viewportMax,
                                        const ImVec2& mouseScreenPosition,
                                        const glm::vec3& planePoint,
                                        const glm::vec3& planeNormal,
                                        glm::vec3& outIntersectionPoint)
        {
            glm::vec3 rayOrigin(0.0f);
            glm::vec3 rayDirection(0.0f);
            if (!TryComputeViewportRay(camera, viewportMin, viewportMax, mouseScreenPosition, rayOrigin, rayDirection))
                return false;

            const float planeNormalLength = glm::length(planeNormal);
            if (planeNormalLength <= 0.000001f)
                return false;

            return TryIntersectRayWithPlane(rayOrigin,
                                            rayDirection,
                                            planePoint,
                                            planeNormal / planeNormalLength,
                                            outIntersectionPoint);
        }

        bool DrawAndHandleColliderGizmos(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return false;

            auto& dragState = GetColliderDragState();
            if (!scene.IsEntityEnabledInHierarchy(selectedEntity))
            {
                if (dragState.Active && dragState.Entity == selectedEntity && undoService)
                    undoService->CancelInteractiveSceneMutation();
                if (dragState.Entity == selectedEntity)
                    dragState = {};
                return false;
            }
            auto& registry = scene.GetRegistry();
            if (!registry.try_get<TransformComponent>(selectedEntity))
                return false;

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
            const glm::mat4 inverseWorldTransform = glm::inverse(worldTransform);
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
            constexpr float handleRadiusPixels = 7.0f;

            // If the selected entity changed mid-drag, abort the pending operation safely.
            if (dragState.Active && dragState.Entity != selectedEntity)
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                dragState = {};
            }

            auto commitOrCancelDrag = [&](bool commit) {
                if (!dragState.Active)
                    return;
                if (undoService)
                {
                    if (commit && dragState.CommitLabel)
                        (void)undoService->CommitInteractiveSceneMutation(dragState.CommitLabel);
                    else
                        undoService->CancelInteractiveSceneMutation();
                }
                dragState = {};
            };

            auto updateLocalPointFromMouse = [&](glm::vec2& localPointOut) -> bool {
                glm::vec3 worldPoint{};
                if (!TryComputeDropWorldPosition(camera, viewportMin, viewportMax, mousePosition, worldPoint))
                    return false;
                const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(worldPoint, 1.0f);
                localPointOut = glm::vec2(localPoint.x, localPoint.y);
                return true;
            };

            auto projectLocalPoint = [&](const glm::vec2& localPoint, ImVec2& outPoint) -> bool {
                const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint.x, localPoint.y, 0.0f, 1.0f);
                return WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldPoint), outPoint);
            };

            if (auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(selectedEntity))
            {
                const glm::vec2 halfSize = boxCollider2D->Size * 0.5f;
                const glm::vec3 localCorners[4] = {
                    glm::vec3(boxCollider2D->Offset.x - halfSize.x, boxCollider2D->Offset.y - halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + halfSize.x, boxCollider2D->Offset.y - halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x + halfSize.x, boxCollider2D->Offset.y + halfSize.y, 0.0f),
                    glm::vec3(boxCollider2D->Offset.x - halfSize.x, boxCollider2D->Offset.y + halfSize.y, 0.0f)
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

                ImVec2 projectedOffset{};
                const glm::vec4 worldOffset = worldTransform * glm::vec4(boxCollider2D->Offset, 0.0f, 1.0f);
                valid = valid && WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldOffset), projectedOffset);
                if (valid)
                {
                    drawList->AddLine(projectedCorners[0], projectedCorners[1], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[1], projectedCorners[2], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[2], projectedCorners[3], IM_COL32(90, 200, 255, 255), 2.0f);
                    drawList->AddLine(projectedCorners[3], projectedCorners[0], IM_COL32(90, 200, 255, 255), 2.0f);

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    for (const ImVec2& point : projectedCorners)
                        drawList->AddCircleFilled(point, handleRadiusPixels, IM_COL32(90, 200, 255, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                        {
                            hoveredHandle = ColliderHandleKind::BoxOffset;
                        }
                        else
                        {
                            for (int i = 0; i < 4; ++i)
                            {
                                if (!IsMouseNearPoint(mousePosition, projectedCorners[i], handleRadiusPixels + 3.0f))
                                    continue;
                                hoveredHandle = static_cast<ColliderHandleKind>(static_cast<uint8_t>(ColliderHandleKind::BoxCorner0) + static_cast<uint8_t>(i));
                                break;
                            }
                        }
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::BoxOffset)
                                ? "Edit Box Collider Offset"
                                : "Edit Box Collider Size";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::BoxOffset ||
                         dragState.Handle == ColliderHandleKind::BoxCorner0 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner1 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner2 ||
                         dragState.Handle == ColliderHandleKind::BoxCorner3))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::BoxOffset)
                            {
                                boxCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                const glm::vec2 delta = localPoint - boxCollider2D->Offset;
                                boxCollider2D->Size = glm::max(glm::abs(glm::vec2(delta.x, delta.y)) * 2.0f, glm::vec2(0.02f));
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
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
                    drawList->AddCircleFilled(centerPoint, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(radiusPoint, handleRadiusPixels, IM_COL32(255, 190, 70, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, centerPoint, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CircleOffset;
                        else if (IsMouseNearPoint(mousePosition, radiusPoint, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CircleRadius;
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::CircleOffset)
                                ? "Edit Circle Collider Offset"
                                : "Edit Circle Collider Radius";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::CircleOffset ||
                         dragState.Handle == ColliderHandleKind::CircleRadius))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::CircleOffset)
                            {
                                circleCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                circleCollider2D->Radius = std::max(0.01f, glm::length(localPoint - circleCollider2D->Offset));
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(selectedEntity))
            {
                ImVec2 projectedOffset{};
                bool valid = projectLocalPoint(polygonCollider2D->Offset, projectedOffset);
                std::vector<ImVec2> projectedPoints(polygonCollider2D->Points.size());
                for (size_t pointIndex = 0; valid && pointIndex < polygonCollider2D->Points.size(); ++pointIndex)
                    valid = projectLocalPoint(polygonCollider2D->Offset + polygonCollider2D->Points[pointIndex], projectedPoints[pointIndex]);

                if (valid && projectedPoints.size() >= 2)
                {
                    for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                    {
                        const ImVec2& pointA = projectedPoints[pointIndex];
                        const ImVec2& pointB = projectedPoints[(pointIndex + 1) % projectedPoints.size()];
                        drawList->AddLine(pointA, pointB, IM_COL32(220, 120, 255, 255), 2.0f);
                    }

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    for (const ImVec2& point : projectedPoints)
                        drawList->AddCircleFilled(point, handleRadiusPixels, IM_COL32(220, 120, 255, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    int hoveredPointIndex = -1;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                        {
                            hoveredHandle = ColliderHandleKind::PolygonOffset;
                        }
                        else
                        {
                            for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                            {
                                if (!IsMouseNearPoint(mousePosition, projectedPoints[pointIndex], handleRadiusPixels + 3.0f))
                                    continue;
                                hoveredHandle = ColliderHandleKind::PolygonPoint;
                                hoveredPointIndex = static_cast<int>(pointIndex);
                                break;
                            }
                        }
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.PointIndex = hoveredPointIndex;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::PolygonOffset)
                                ? "Edit Polygon Collider Offset"
                                : "Edit Polygon Collider Point";
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::PolygonOffset || dragState.Handle == ColliderHandleKind::PolygonPoint))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::PolygonOffset)
                            {
                                polygonCollider2D->Offset = localPoint;
                            }
                            else if (dragState.PointIndex >= 0 && dragState.PointIndex < static_cast<int>(polygonCollider2D->Points.size()))
                            {
                                polygonCollider2D->Points[static_cast<size_t>(dragState.PointIndex)] = localPoint - polygonCollider2D->Offset;
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(selectedEntity))
            {
                ImVec2 projectedOffset{};
                ImVec2 projectedPointA{};
                ImVec2 projectedPointB{};
                const bool valid = projectLocalPoint(edgeCollider2D->Offset, projectedOffset) &&
                                   projectLocalPoint(edgeCollider2D->Offset + edgeCollider2D->PointA, projectedPointA) &&
                                   projectLocalPoint(edgeCollider2D->Offset + edgeCollider2D->PointB, projectedPointB);
                if (valid)
                {
                    drawList->AddLine(projectedPointA, projectedPointB, IM_COL32(255, 110, 110, 255), 2.0f);
                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(projectedPointA, handleRadiusPixels, IM_COL32(255, 110, 110, 245));
                    drawList->AddCircleFilled(projectedPointB, handleRadiusPixels, IM_COL32(255, 110, 110, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgeOffset;
                        else if (IsMouseNearPoint(mousePosition, projectedPointA, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgePointA;
                        else if (IsMouseNearPoint(mousePosition, projectedPointB, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::EdgePointB;
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::EdgeOffset)
                                ? "Edit Edge Collider Offset"
                                : "Edit Edge Collider Point";
                            dragState.PointIndex = -1;
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::EdgeOffset ||
                         dragState.Handle == ColliderHandleKind::EdgePointA ||
                         dragState.Handle == ColliderHandleKind::EdgePointB))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::EdgeOffset)
                            {
                                edgeCollider2D->Offset = localPoint;
                            }
                            else if (dragState.Handle == ColliderHandleKind::EdgePointA)
                            {
                                edgeCollider2D->PointA = localPoint - edgeCollider2D->Offset;
                            }
                            else
                            {
                                edgeCollider2D->PointB = localPoint - edgeCollider2D->Offset;
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(selectedEntity))
            {
                const glm::vec2 halfSize = capsuleCollider2D->Size * 0.5f;
                const bool vertical = capsuleCollider2D->Direction == CapsuleCollider2DComponent::Orientation::Vertical;
                const float radius = vertical ? halfSize.x : halfSize.y;
                const float segmentHalf = vertical
                    ? std::max(0.0f, halfSize.y - radius)
                    : std::max(0.0f, halfSize.x - radius);
                const glm::vec2 centerA = capsuleCollider2D->Offset + (vertical ? glm::vec2(0.0f, -segmentHalf) : glm::vec2(-segmentHalf, 0.0f));
                const glm::vec2 centerB = capsuleCollider2D->Offset + (vertical ? glm::vec2(0.0f, segmentHalf) : glm::vec2(segmentHalf, 0.0f));
                const glm::vec2 cornerPoint = capsuleCollider2D->Offset + halfSize;

                ImVec2 projectedOffset{};
                ImVec2 projectedCenterA{};
                ImVec2 projectedCenterB{};
                ImVec2 projectedRadiusPointA{};
                ImVec2 projectedRadiusPointB{};
                ImVec2 projectedCorner{};
                const bool valid = projectLocalPoint(capsuleCollider2D->Offset, projectedOffset) &&
                                   projectLocalPoint(centerA, projectedCenterA) &&
                                   projectLocalPoint(centerB, projectedCenterB) &&
                                   projectLocalPoint(centerA + (vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius)), projectedRadiusPointA) &&
                                   projectLocalPoint(centerB + (vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius)), projectedRadiusPointB) &&
                                   projectLocalPoint(cornerPoint, projectedCorner);
                if (valid)
                {
                    const float radiusPixelsA = std::sqrt((projectedRadiusPointA.x - projectedCenterA.x) * (projectedRadiusPointA.x - projectedCenterA.x) +
                                                          (projectedRadiusPointA.y - projectedCenterA.y) * (projectedRadiusPointA.y - projectedCenterA.y));
                    const float radiusPixelsB = std::sqrt((projectedRadiusPointB.x - projectedCenterB.x) * (projectedRadiusPointB.x - projectedCenterB.x) +
                                                          (projectedRadiusPointB.y - projectedCenterB.y) * (projectedRadiusPointB.y - projectedCenterB.y));
                    drawList->AddCircle(projectedCenterA, radiusPixelsA, IM_COL32(120, 255, 180, 255), 32, 2.0f);
                    drawList->AddCircle(projectedCenterB, radiusPixelsB, IM_COL32(120, 255, 180, 255), 32, 2.0f);

                    ImVec2 sideA0{};
                    ImVec2 sideA1{};
                    ImVec2 sideB0{};
                    ImVec2 sideB1{};
                    const glm::vec2 sideOffset0 = vertical ? glm::vec2(radius, 0.0f) : glm::vec2(0.0f, radius);
                    const glm::vec2 sideOffset1 = vertical ? glm::vec2(-radius, 0.0f) : glm::vec2(0.0f, -radius);
                    if (projectLocalPoint(centerA + sideOffset0, sideA0) &&
                        projectLocalPoint(centerB + sideOffset0, sideA1) &&
                        projectLocalPoint(centerA + sideOffset1, sideB0) &&
                        projectLocalPoint(centerB + sideOffset1, sideB1))
                    {
                        drawList->AddLine(sideA0, sideA1, IM_COL32(120, 255, 180, 255), 2.0f);
                        drawList->AddLine(sideB0, sideB1, IM_COL32(120, 255, 180, 255), 2.0f);
                    }

                    drawList->AddCircleFilled(projectedOffset, handleRadiusPixels, IM_COL32(75, 220, 140, 245));
                    drawList->AddCircleFilled(projectedCorner, handleRadiusPixels, IM_COL32(120, 255, 180, 245));

                    ColliderHandleKind hoveredHandle = ColliderHandleKind::None;
                    if (canEdit && mouseInViewport && !dragState.Active)
                    {
                        if (IsMouseNearPoint(mousePosition, projectedOffset, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CapsuleOffset;
                        else if (IsMouseNearPoint(mousePosition, projectedCorner, handleRadiusPixels + 3.0f))
                            hoveredHandle = ColliderHandleKind::CapsuleCorner;
                    }

                    if (hoveredHandle != ColliderHandleKind::None)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = hoveredHandle;
                            dragState.CommitLabel = (hoveredHandle == ColliderHandleKind::CapsuleOffset)
                                ? "Edit Capsule Collider Offset"
                                : "Edit Capsule Collider Size";
                            dragState.PointIndex = -1;
                        }
                    }

                    if (dragState.Active && dragState.Entity == selectedEntity &&
                        (dragState.Handle == ColliderHandleKind::CapsuleOffset || dragState.Handle == ColliderHandleKind::CapsuleCorner))
                    {
                        glm::vec2 localPoint(0.0f);
                        if (updateLocalPointFromMouse(localPoint))
                        {
                            if (dragState.Handle == ColliderHandleKind::CapsuleOffset)
                            {
                                capsuleCollider2D->Offset = localPoint;
                            }
                            else
                            {
                                const glm::vec2 delta = localPoint - capsuleCollider2D->Offset;
                                capsuleCollider2D->Size = glm::max(glm::abs(delta) * 2.0f, glm::vec2(0.02f));
                            }
                        }

                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                            commitOrCancelDrag(true);
                    }
                }
            }

            if (dragState.Active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                commitOrCancelDrag(true);

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

            return dragState.Active;
        }

        bool DrawAndHandleLightingGizmos(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            if (!drawList || selectedEntity == entt::null || !scene.IsValid(selectedEntity))
                return false;

            auto& dragState = GetLightingDragState();
            if (!scene.IsEntityEnabledInHierarchy(selectedEntity))
            {
                if (dragState.Active && dragState.Entity == selectedEntity && undoService)
                    undoService->CancelInteractiveSceneMutation();
                if (dragState.Entity == selectedEntity)
                    dragState = {};
                return false;
            }
            auto& registry = scene.GetRegistry();
            auto* transform = registry.try_get<TransformComponent>(selectedEntity);
            if (!transform)
                return false;

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
            const glm::mat4 inverseWorldTransform = glm::inverse(worldTransform);
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
            constexpr float handleRadiusPixels = 7.0f;

            if (dragState.Active && dragState.Entity != selectedEntity)
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                dragState = {};
            }

            auto commitOrCancelDrag = [&](bool commit) {
                if (!dragState.Active)
                    return;
                if (undoService)
                {
                    if (commit && dragState.CommitLabel)
                        (void)undoService->CommitInteractiveSceneMutation(dragState.CommitLabel);
                    else
                        undoService->CancelInteractiveSceneMutation();
                }
                dragState = {};
            };

            auto updateWorldPointFromMouse = [&](glm::vec3& worldPointOut) -> bool {
                return TryComputeDropWorldPosition(camera, viewportMin, viewportMax, mousePosition, worldPointOut);
            };

            if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity))
            {
                glm::vec2 worldDirection = directionalLight->UseEntityRotation
                    ? glm::vec2(worldTransform[0].x, worldTransform[0].y)
                    : directionalLight->Direction;
                if (glm::length(worldDirection) <= 0.0001f)
                    worldDirection = glm::vec2(0.0f, -1.0f);
                worldDirection = glm::normalize(worldDirection);

                const glm::vec3 worldOrigin = glm::vec3(worldTransform[3]);
                constexpr float arrowLengthWorld = 1.5f;
                const glm::vec3 worldEndpoint = worldOrigin + glm::vec3(worldDirection * arrowLengthWorld, 0.0f);

                ImVec2 originPoint{};
                ImVec2 endpointPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldOrigin, originPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldEndpoint, endpointPoint))
                {
                    drawList->AddLine(originPoint, endpointPoint, IM_COL32(255, 230, 110, 240), 2.0f);
                    drawList->AddCircleFilled(originPoint, handleRadiusPixels - 1.0f, IM_COL32(255, 230, 110, 220));
                    drawList->AddCircleFilled(endpointPoint, handleRadiusPixels, IM_COL32(255, 180, 60, 245));

                    if (canEdit && mouseInViewport && !dragState.Active &&
                        IsMouseNearPoint(mousePosition, endpointPoint, handleRadiusPixels + 3.0f))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = LightingHandleKind::DirectionalDirection;
                            dragState.PointIndex = -1;
                            dragState.CommitLabel = directionalLight->UseEntityRotation
                                ? "Edit Directional Light Rotation"
                                : "Edit Directional Light Direction";
                        }
                    }
                }
            }

            if (auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity))
            {
                const glm::vec4 worldCenter4 = worldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                const glm::vec4 worldRadiusPoint4 = worldTransform * glm::vec4(pointLight->Radius, 0.0f, 0.0f, 1.0f);

                ImVec2 centerPoint{};
                ImVec2 radiusPoint{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldCenter4), centerPoint) &&
                    WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldRadiusPoint4), radiusPoint))
                {
                    const float radiusPixels = std::sqrt((radiusPoint.x - centerPoint.x) * (radiusPoint.x - centerPoint.x) +
                                                         (radiusPoint.y - centerPoint.y) * (radiusPoint.y - centerPoint.y));
                    drawList->AddCircle(centerPoint, radiusPixels, IM_COL32(255, 185, 80, 235), 64, 2.0f);
                    drawList->AddCircleFilled(radiusPoint, handleRadiusPixels, IM_COL32(255, 185, 80, 245));

                    if (canEdit && mouseInViewport && !dragState.Active &&
                        IsMouseNearPoint(mousePosition, radiusPoint, handleRadiusPixels + 3.0f))
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            if (undoService)
                                undoService->BeginInteractiveSceneMutation();
                            dragState.Active = true;
                            dragState.Entity = selectedEntity;
                            dragState.Handle = LightingHandleKind::PointRadius;
                            dragState.PointIndex = -1;
                            dragState.CommitLabel = "Edit Point Light Radius";
                        }
                    }
                }
            }

            if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
            {
                if (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::ManualPolygon &&
                    shadowOccluder->PolygonPoints.size() >= 2)
                {
                    std::vector<ImVec2> projectedPoints;
                    projectedPoints.reserve(shadowOccluder->PolygonPoints.size());
                    for (const glm::vec2& localPoint : shadowOccluder->PolygonPoints)
                    {
                        const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint, 0.0f, 1.0f);
                        ImVec2 projectedPoint{};
                        if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, glm::vec3(worldPoint), projectedPoint))
                            continue;
                        projectedPoints.push_back(projectedPoint);
                    }

                    if (projectedPoints.size() >= 2)
                    {
                        for (size_t pointIndex = 0; pointIndex + 1 < projectedPoints.size(); ++pointIndex)
                            drawList->AddLine(projectedPoints[pointIndex], projectedPoints[pointIndex + 1], IM_COL32(140, 240, 255, 235), 2.0f);
                        if (shadowOccluder->Closed && projectedPoints.size() >= 3)
                            drawList->AddLine(projectedPoints.back(), projectedPoints.front(), IM_COL32(140, 240, 255, 235), 2.0f);

                        for (size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                        {
                            drawList->AddCircleFilled(projectedPoints[pointIndex], handleRadiusPixels - 1.0f, IM_COL32(90, 200, 255, 240));
                            if (!canEdit || !mouseInViewport || dragState.Active)
                                continue;
                            if (!IsMouseNearPoint(mousePosition, projectedPoints[pointIndex], handleRadiusPixels + 3.0f))
                                continue;
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                if (undoService)
                                    undoService->BeginInteractiveSceneMutation();
                                dragState.Active = true;
                                dragState.Entity = selectedEntity;
                                dragState.Handle = LightingHandleKind::OccluderPoint;
                                dragState.PointIndex = static_cast<int>(pointIndex);
                                dragState.CommitLabel = "Edit Shadow Occluder Point";
                            }
                            break;
                        }
                    }
                }
            }

            if (dragState.Active && dragState.Entity == selectedEntity)
            {
                glm::vec3 mouseWorldPoint(0.0f);
                const bool hasMouseWorldPoint = updateWorldPointFromMouse(mouseWorldPoint);

                if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::DirectionalDirection)
                {
                    if (auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity))
                    {
                        const glm::vec3 worldOrigin = glm::vec3(worldTransform[3]);
                        glm::vec2 direction = glm::vec2(mouseWorldPoint.x - worldOrigin.x, mouseWorldPoint.y - worldOrigin.y);
                        if (glm::length(direction) > 0.0001f)
                        {
                            direction = glm::normalize(direction);
                            if (directionalLight->UseEntityRotation)
                            {
                                if (auto* mutableTransform = registry.try_get<TransformComponent>(selectedEntity))
                                {
                                    mutableTransform->Rotation.z = glm::degrees(std::atan2(direction.y, direction.x));
                                    scene.MarkTransformDirty(selectedEntity);
                                }
                            }
                            else
                            {
                                directionalLight->Direction = direction;
                            }
                        }
                    }
                }
                else if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::PointRadius)
                {
                    if (auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity))
                    {
                        const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(mouseWorldPoint, 1.0f);
                        pointLight->Radius = std::max(0.01f, glm::length(glm::vec2(localPoint.x, localPoint.y)));
                    }
                }
                else if (hasMouseWorldPoint && dragState.Handle == LightingHandleKind::OccluderPoint && dragState.PointIndex >= 0)
                {
                    if (auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity))
                    {
                        if (dragState.PointIndex < static_cast<int>(shadowOccluder->PolygonPoints.size()))
                        {
                            const glm::vec4 localPoint = inverseWorldTransform * glm::vec4(mouseWorldPoint, 1.0f);
                            shadowOccluder->PolygonPoints[dragState.PointIndex] = glm::vec2(localPoint.x, localPoint.y);
                        }
                    }
                }

                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    commitOrCancelDrag(true);
            }

            if (dragState.Active && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                commitOrCancelDrag(true);

            return dragState.Active;
        }

        // -----------------------------------------------------------------
        // Grid2D + TilemapLayerComponent editing
        // -----------------------------------------------------------------

        /// Compute the first cell center for a Grid2D/TilemapLayer combination.
        glm::vec2 GetGrid2DFirstCellCenter(const Grid2DComponent& grid, const TilemapLayerComponent& layer)
        {
            const int32_t gridWidth  = std::max(1, layer.GridSize.x);
            const int32_t gridHeight = std::max(1, layer.GridSize.y);
            const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
            return -0.5f * glm::vec2(gridWidth - 1, gridHeight - 1) * cellSize;
        }

        bool TryGetGrid2DHoveredCell(const Camera& camera,
                                     const glm::mat4& worldTransform,
                                     const Grid2DComponent& grid,
                                     const TilemapLayerComponent& layer,
                                     const ImVec2& viewportMin,
                                     const ImVec2& viewportMax,
                                     const ImVec2& mousePosition,
                                     glm::ivec2& outCell)
        {
            glm::vec3 rayOrigin(0.0f);
            glm::vec3 rayDirection(0.0f);
            if (!TryComputeViewportRay(camera, viewportMin, viewportMax, mousePosition, rayOrigin, rayDirection))
                return false;

            glm::vec3 planeNormal = glm::vec3(worldTransform[2]);
            if (glm::length(planeNormal) <= 0.000001f)
                planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            else
                planeNormal = glm::normalize(planeNormal);

            const glm::vec3 planePoint = glm::vec3(worldTransform[3]);
            glm::vec3 worldPosition(0.0f);
            if (!TryIntersectRayWithPlane(rayOrigin, rayDirection, planePoint, planeNormal, worldPosition))
                return false;

            const glm::mat4 inverseTransform = glm::inverse(worldTransform);
            const glm::vec4 localPosition = inverseTransform * glm::vec4(worldPosition, 1.0f);
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(grid, layer);
            const glm::vec2 cellSize(std::max(0.001f, grid.CellSize.x), std::max(0.001f, grid.CellSize.y));
            const glm::vec2 mapMin = firstCellCenter - cellSize * 0.5f;
            const int32_t cellX = static_cast<int32_t>(std::floor((localPosition.x - mapMin.x) / cellSize.x));
            const int32_t cellY = static_cast<int32_t>(std::floor((localPosition.y - mapMin.y) / cellSize.y));
            if (!IsLayerCellInBounds(layer, cellX, cellY))
                return false;
            outCell = glm::ivec2(cellX, cellY);
            return true;
        }

        /// Simplified Grid2D tilemap editing for the new component architecture.
        /// Uses the active palette tile IDs to paint onto TilemapLayerComponent cells.
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
                                        const std::string& activePaletteKey)
        {
            tilemapEditorState.HasHoveredCell = false;
            if (!drawList || gridEntity == entt::null || layerEntity == entt::null)
                return false;
            if (!tilemapEditorState.Enabled)
                return false;
            if (!scene.IsValid(gridEntity) || !scene.IsValid(layerEntity))
                return false;

            auto& registry = scene.GetRegistry();
            auto* grid = registry.try_get<Grid2DComponent>(gridEntity);
            auto* layer = registry.try_get<TilemapLayerComponent>(layerEntity);
            if (!grid || !layer)
                return false;

            layer->EnsureStorage();
            tilemapEditorState.BrushSize = std::max(1, tilemapEditorState.BrushSize);

            const ImVec2 mousePosition = ImGui::GetMousePos();
            const bool mouseInViewport = mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
                                         mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;

            const float fixedDelta = Time::GetFixedDeltaTimeSeconds();
            const float interpolationAlpha = (fixedDelta > 0.0f)
                ? std::clamp(Time::GetFixedTimeAccumulatorSeconds() / fixedDelta, 0.0f, 1.0f)
                : 1.0f;
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(gridEntity, interpolationAlpha);

            glm::ivec2 hoveredCell(0);
            const bool hasHoveredCell = mouseInViewport &&
                TryGetGrid2DHoveredCell(camera, worldTransform, *grid, *layer,
                                        viewportMin, viewportMax, mousePosition, hoveredCell);
            if (hasHoveredCell)
            {
                tilemapEditorState.HasHoveredCell = true;
                tilemapEditorState.HoveredCell = hoveredCell;
            }

            const glm::vec2 cellSize(std::max(0.001f, grid->CellSize.x), std::max(0.001f, grid->CellSize.y));
            const glm::vec2 firstCellCenter = GetGrid2DFirstCellCenter(*grid, *layer);
            const glm::vec2 gridBoundaryMin = firstCellCenter - cellSize * 0.5f;
            const glm::vec2 gridBoundaryMax = gridBoundaryMin + glm::vec2(layer->GridSize) * cellSize;

            // Grid overlay -- uses near-plane clipping so lines that are
            // partially behind the camera still render their visible portion.
            if (tilemapEditorState.ShowGridOverlay)
            {
                for (int32_t x = 0; x <= std::max(1, layer->GridSize.x); ++x)
                {
                    const float localX = gridBoundaryMin.x + static_cast<float>(x) * cellSize.x;
                    const glm::vec3 worldStart = glm::vec3(worldTransform * glm::vec4(localX, gridBoundaryMin.y, 0.0f, 1.0f));
                    const glm::vec3 worldEnd   = glm::vec3(worldTransform * glm::vec4(localX, gridBoundaryMax.y, 0.0f, 1.0f));
                    ImVec2 screenStart, screenEnd;
                    if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                            worldStart, worldEnd, screenStart, screenEnd))
                    {
                        drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                    }
                }
                for (int32_t y = 0; y <= std::max(1, layer->GridSize.y); ++y)
                {
                    const float localY = gridBoundaryMin.y + static_cast<float>(y) * cellSize.y;
                    const glm::vec3 worldStart = glm::vec3(worldTransform * glm::vec4(gridBoundaryMin.x, localY, 0.0f, 1.0f));
                    const glm::vec3 worldEnd   = glm::vec3(worldTransform * glm::vec4(gridBoundaryMax.x, localY, 0.0f, 1.0f));
                    ImVec2 screenStart, screenEnd;
                    if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                            worldStart, worldEnd, screenStart, screenEnd))
                    {
                        drawList->AddLine(screenStart, screenEnd, IM_COL32(80, 170, 255, 120), 1.0f);
                    }
                }
            }

            // Cell highlight helper -- clips each edge individually so the
            // highlight remains visible when corners go behind the camera.
            auto drawCellHighlight = [&](const glm::ivec2& cell, ImU32 color, float thickness) {
                const glm::vec2 localCellCenter = firstCellCenter + glm::vec2(
                    static_cast<float>(cell.x) * cellSize.x,
                    static_cast<float>(cell.y) * cellSize.y);
                const glm::vec2 localMin = localCellCenter - cellSize * 0.5f;
                const glm::vec2 localMax = localCellCenter + cellSize * 0.5f;
                const glm::vec3 worldCorners[4] = {
                    glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, 0.0f, 1.0f)),
                    glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, 0.0f, 1.0f)),
                    glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, 0.0f, 1.0f)),
                    glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, 0.0f, 1.0f))
                };
                for (int i = 0; i < 4; ++i)
                {
                    ImVec2 screenA, screenB;
                    if (ProjectLineSegmentClipped(camera, viewportMin, viewportWidth, viewportHeight,
                            worldCorners[i], worldCorners[(i + 1) % 4], screenA, screenB))
                    {
                        drawList->AddLine(screenA, screenB, color, thickness);
                    }
                }
            };

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            // Avoid gating painting on ImGui's active-item state; non-interactive
            // viewport items (e.g. scene image) can keep an item active and block paint.
            const bool canCaptureMouse = canEdit && mouseInViewport;
            const bool leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

            // Resolve the active tile's TileTable entry so rendering can find it.
            const uint32_t paintTileValue = [&]() -> uint32_t {
                if (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    return 0u;
                if (!tilemapEditorState.ActiveTileAssetKey.empty())
                    return layer->GetOrAddTileTableEntry(tilemapEditorState.ActiveTileAssetKey);
                if (!tilemapEditorState.StampTileAssetKeys.empty() &&
                    !tilemapEditorState.StampTileAssetKeys[0].empty())
                {
                    return layer->GetOrAddTileTableEntry(tilemapEditorState.StampTileAssetKeys[0]);
                }
                return 0u;
            }();

            // Undo-aware painting using Grid2DPaintDragState.
            auto& paintDragState = GetGrid2DPaintDragState();

            auto finalizeGrid2DStroke = [&](const char* label) {
                if (!paintDragState.Active && paintDragState.PendingEdits.empty())
                    return;
                if (!undoService)
                {
                    paintDragState = {};
                    return;
                }

                std::vector<Grid2DCellEdit> edits;
                edits.reserve(paintDragState.PendingEdits.size());
                for (auto& [_, edit] : paintDragState.PendingEdits)
                    edits.push_back(edit);

                if (!edits.empty())
                {
                    auto command = std::make_unique<Grid2DPaintCommand>(
                        label ? std::string(label) : std::string("Paint Grid2D"),
                        undoService, layerEntity, std::move(edits));
                    (void)undoService->ExecuteCommand(std::move(command));
                }
                paintDragState = {};
            };

            // If layer changed mid-stroke, finalize the previous one.
            if (paintDragState.Active && paintDragState.LayerEntity != layerEntity)
                finalizeGrid2DStroke("Paint Grid2D");

            // Begin stroke on mouse press.
            const bool leftMousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (canCaptureMouse && leftMousePressed && hasHoveredCell)
            {
                paintDragState.Active = true;
                paintDragState.LayerEntity = layerEntity;
                paintDragState.PendingEdits.clear();
            }

            // Continue stroke while mouse is held.
            if (paintDragState.Active && paintDragState.LayerEntity == layerEntity &&
                hasHoveredCell && canCaptureMouse && leftMouseDown)
            {
                if (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*layer,
                                hoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
                                0u, paintDragState);
                }
                else if (tilemapEditorState.HasStamp() &&
                         (tilemapEditorState.StampSize.x > 1 || tilemapEditorState.StampSize.y > 1))
                {
                    for (int32_t sy = 0; sy < tilemapEditorState.StampSize.y; ++sy)
                    {
                        for (int32_t sx = 0; sx < tilemapEditorState.StampSize.x; ++sx)
                        {
                            // Flip stamp Y: palette row 0 (visual top) maps to the
                            // highest scene Y (visual top), since scene Y-axis is up
                            // while palette Y-axis is down.
                            const int32_t sceneYOffset = tilemapEditorState.StampSize.y - 1 - sy;
                            const glm::ivec2 cell = hoveredCell + glm::ivec2(sx, sceneYOffset);
                            const size_t stampIdx = static_cast<size_t>(
                                sy * tilemapEditorState.StampSize.x + sx);
                            if (stampIdx >= tilemapEditorState.StampTileAssetKeys.size())
                                continue;
                            const std::string& stampKey = tilemapEditorState.StampTileAssetKeys[stampIdx];
                            const uint32_t resolvedId = stampKey.empty()
                                ? 0u : layer->GetOrAddTileTableEntry(stampKey);
                            StageGrid2DEdit(*layer, cell, resolvedId, paintDragState);
                        }
                    }
                }
                else
                {
                    const int32_t brushSize = std::max(1, tilemapEditorState.BrushSize);
                    const int32_t startOffset = (brushSize - 1) / 2;
                    for (int32_t by = 0; by < brushSize; ++by)
                        for (int32_t bx = 0; bx < brushSize; ++bx)
                            StageGrid2DEdit(*layer,
                                hoveredCell - glm::ivec2(startOffset) + glm::ivec2(bx, by),
                                paintTileValue, paintDragState);
                }
            }

            // Finalize stroke on mouse release.
            if (paintDragState.Active && !leftMouseDown)
            {
                const char* label = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    ? "Erase Grid2D Tiles" : "Paint Grid2D Tiles";
                finalizeGrid2DStroke(label);
            }

            // Hover highlight.
            if (hasHoveredCell)
            {
                const ImU32 highlightColor = (tilemapEditorState.PaintMode == TilemapPaintMode::Erase)
                    ? IM_COL32(255, 80, 80, 200) : IM_COL32(85, 200, 255, 200);
                drawCellHighlight(hoveredCell, highlightColor, 2.0f);

                // Stamp preview.
                if (tilemapEditorState.HasStamp() &&
                    (tilemapEditorState.StampSize.x > 1 || tilemapEditorState.StampSize.y > 1))
                {
                    for (int32_t sy = 0; sy < tilemapEditorState.StampSize.y; ++sy)
                    {
                        for (int32_t sx = 0; sx < tilemapEditorState.StampSize.x; ++sx)
                        {
                            const int32_t sceneYOffset = tilemapEditorState.StampSize.y - 1 - sy;
                            const glm::ivec2 previewCell = hoveredCell + glm::ivec2(sx, sceneYOffset);
                            if (previewCell == hoveredCell)
                                continue;
                            if (IsLayerCellInBounds(*layer, previewCell.x, previewCell.y))
                                drawCellHighlight(previewCell, IM_COL32(85, 200, 255, 100), 1.0f);
                        }
                    }
                }
            }

            return hasHoveredCell;
        }

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
                const glm::vec4 world = model * kQuadLocalPositions[i];
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
        // Transform gizmo constants
        // -----------------------------------------------------------------

        constexpr float kGizmoAxisLength = 80.0f;
        constexpr float kGizmoArrowSize = 10.0f;
        constexpr float kGizmoHandleRadius = 7.0f;
        constexpr float kGizmoHitRadius = 12.0f;
        constexpr float kGizmoPlaneHandleSize = 22.0f;
        constexpr float kGizmoRotateRadius = 60.0f;
        constexpr float kGizmoScaleBoxSize = 6.0f;
        constexpr ImU32 kGizmoColorX = IM_COL32(230, 60, 60, 255);
        constexpr ImU32 kGizmoColorY = IM_COL32(80, 200, 60, 255);
        constexpr ImU32 kGizmoColorZ = IM_COL32(60, 120, 230, 255);
        constexpr ImU32 kGizmoColorXY = IM_COL32(255, 220, 60, 180);
        constexpr ImU32 kGizmoColorActive = IM_COL32(255, 220, 60, 255);
        constexpr ImU32 kGizmoColorRotateRing = IM_COL32(120, 180, 255, 200);

        ImU32 AxisColor(int axis, int activeAxis)
        {
            if (axis == activeAxis) return kGizmoColorActive;
            switch (axis)
            {
                case 0: return kGizmoColorX;
                case 1: return kGizmoColorY;
                case 2: return kGizmoColorZ;
                default: return IM_COL32(200, 200, 200, 255);
            }
        }

        // -----------------------------------------------------------------
        // Transform gizmo drawing and interaction
        // -----------------------------------------------------------------

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
                                          TransformGizmoState& gizmoState)
        {
            if (!drawList || gizmoState.Mode == TransformGizmoMode::None)
                return false;
            if (selectedEntity == entt::null || !scene.IsValid(selectedEntity))
            {
                if (gizmoState.DragActive)
                {
                    if (undoService) undoService->CancelInteractiveSceneMutation();
                    gizmoState.DragActive = false;
                }
                return false;
            }

            auto& registry = scene.GetRegistry();
            auto* transform = registry.try_get<TransformComponent>(selectedEntity);
            if (!transform)
                return false;

            const bool canEdit = playModeState == EditorPlayModeState::Edit;
            const ImVec2 mousePos = ImGui::GetMousePos();
            const bool mouseInViewport = mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                         mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y;

            // Compute gizmo origin in screen space from entity world position
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(selectedEntity);
            const glm::vec3 entityWorldPos = glm::vec3(worldTransform[3]);
            ImVec2 originScreen{};
            if (!WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, entityWorldPos, originScreen))
                return false;

            // Compute screen-space scale factor so gizmo stays a consistent size
            // regardless of camera zoom/distance.
            float pixelsPerUnit = 1.0f;
            {
                const glm::vec3 offsetPoint = entityWorldPos + glm::vec3(1.0f, 0.0f, 0.0f);
                ImVec2 offsetScreen{};
                if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, offsetPoint, offsetScreen))
                {
                    const float dx = offsetScreen.x - originScreen.x;
                    const float dy = offsetScreen.y - originScreen.y;
                    pixelsPerUnit = std::sqrt(dx * dx + dy * dy);
                }
            }
            if (pixelsPerUnit < 0.001f)
                return false;

            // Axis endpoints in screen space
            const float worldAxisLength = kGizmoAxisLength / pixelsPerUnit;
            std::array<ImVec2, 3> axisEnds{};
            std::array<bool, 3> axisVisible{};
            std::array<float, 3> axisScreenLengths{};
            for (int axis = 0; axis < 3; ++axis)
            {
                const glm::vec3 worldEnd = entityWorldPos + GetGizmoAxisDirection(axis) * worldAxisLength;
                ImVec2 clippedStart{};
                ImVec2 clippedEnd{};
                axisVisible[axis] = ProjectLineSegmentClipped(camera,
                                                              viewportMin,
                                                              viewportWidth,
                                                              viewportHeight,
                                                              entityWorldPos,
                                                              worldEnd,
                                                              clippedStart,
                                                              clippedEnd);
                axisEnds[axis] = axisVisible[axis] ? clippedEnd : originScreen;
                const float dx = axisEnds[axis].x - originScreen.x;
                const float dy = axisEnds[axis].y - originScreen.y;
                axisScreenLengths[axis] = std::sqrt(dx * dx + dy * dy);
            }

            // If the selected entity changed mid-drag, abort
            if (gizmoState.DragActive && gizmoState.DragEntity != selectedEntity)
            {
                if (undoService) undoService->CancelInteractiveSceneMutation();
                gizmoState.DragActive = false;
            }

            const int activeAxis = gizmoState.DragActive ? gizmoState.DragAxis : -1;

            // ---- TRANSLATE GIZMO ----
            if (gizmoState.Mode == TransformGizmoMode::Translate)
            {
                auto drawArrowHead = [&](const ImVec2& endPoint, int axis) {
                    const float dx = endPoint.x - originScreen.x;
                    const float dy = endPoint.y - originScreen.y;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    if (length <= 1.0f)
                        return;

                    const float nx = dx / length;
                    const float ny = dy / length;
                    const ImVec2 base1(endPoint.x - nx * kGizmoArrowSize - ny * kGizmoArrowSize * 0.4f,
                                       endPoint.y - ny * kGizmoArrowSize + nx * kGizmoArrowSize * 0.4f);
                    const ImVec2 base2(endPoint.x - nx * kGizmoArrowSize + ny * kGizmoArrowSize * 0.4f,
                                       endPoint.y - ny * kGizmoArrowSize - nx * kGizmoArrowSize * 0.4f);
                    drawList->AddTriangleFilled(endPoint, base1, base2, AxisColor(axis, activeAxis));
                };

                // Draw axes
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!axisVisible[axis] || axisScreenLengths[axis] <= 1.0f)
                        continue;

                    drawList->AddLine(originScreen, axisEnds[axis], AxisColor(axis, activeAxis), 2.5f);
                    drawArrowHead(axisEnds[axis], axis);
                }

                // XY plane handle (small square at offset)
                const float planeOffset = kGizmoPlaneHandleSize;
                const ImVec2 planeCorner(originScreen.x + planeOffset, originScreen.y - planeOffset);
                drawList->AddRectFilled(
                    ImVec2(originScreen.x + planeOffset * 0.3f, originScreen.y - planeOffset * 0.3f),
                    planeCorner,
                    activeAxis == 3 ? kGizmoColorActive : kGizmoColorXY);

                // Hit testing for translate handles
                int hoveredAxis = -1;
                if (canEdit && mouseInViewport && !gizmoState.DragActive)
                {
                    // Check XY plane handle first (axis=3 means XY plane)
                    const ImVec2 planeMin(originScreen.x + planeOffset * 0.3f, originScreen.y - planeOffset);
                    const ImVec2 planeMax(originScreen.x + planeOffset, originScreen.y - planeOffset * 0.3f);
                    if (mousePos.x >= planeMin.x && mousePos.x <= planeMax.x &&
                        mousePos.y >= planeMin.y && mousePos.y <= planeMax.y)
                    {
                        hoveredAxis = 3; // XY plane
                    }

                    if (hoveredAxis < 0)
                    {
                        float bestDistance = kGizmoHitRadius + 1.0f;
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                                continue;

                            const float distance = DistanceToLineSegment(mousePos, originScreen, axisEnds[axis]);
                            if (distance <= kGizmoHitRadius && distance < bestDistance)
                            {
                                bestDistance = distance;
                                hoveredAxis = axis;
                            }
                        }
                    }
                }

                if (hoveredAxis >= 0)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                // Begin drag
                if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        undoService->BeginInteractiveSceneMutation();
                    gizmoState.DragActive = true;
                    gizmoState.DragAxis = hoveredAxis;
                    gizmoState.DragEntity = selectedEntity;
                    gizmoState.DragStartEntityPosition = transform->Position;
                    gizmoState.DragStartGizmoOrigin = entityWorldPos;
                    gizmoState.DragStartMousePosition = glm::vec2(mousePos.x, mousePos.y);
                    gizmoState.DragReferenceValue = worldAxisLength;

                    if (hoveredAxis == 3)
                    {
                        gizmoState.DragPlaneNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                    }
                    else
                    {
                        gizmoState.DragPlaneNormal = ComputeAxisDragPlaneNormal(camera,
                                                                               GetGizmoAxisDirection(hoveredAxis),
                                                                               entityWorldPos);
                    }

                    glm::vec3 mouseWorld{};
                    if (!TryIntersectMouseWithPlane(camera,
                                                    viewportMin,
                                                    viewportMax,
                                                    mousePos,
                                                    gizmoState.DragStartGizmoOrigin,
                                                    gizmoState.DragPlaneNormal,
                                                    mouseWorld))
                    {
                        mouseWorld = gizmoState.DragStartGizmoOrigin;
                    }
                    gizmoState.DragStartWorldPosition = mouseWorld;

                    // Snapshot multi-selection positions
                    gizmoState.DragEntities.clear();
                    gizmoState.DragStartPositions.clear();
                    for (entt::entity e : multiSelectedEntities)
                    {
                        if (e != selectedEntity && scene.IsValid(e))
                        {
                            auto* t = registry.try_get<TransformComponent>(e);
                            if (t)
                            {
                                gizmoState.DragEntities.push_back(e);
                                gizmoState.DragStartPositions.push_back(t->Position);
                            }
                        }
                    }
                }

                // Continue drag
                if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                    gizmoState.Mode == TransformGizmoMode::Translate)
                {
                    glm::vec3 currentMouseWorld{};
                    if (TryIntersectMouseWithPlane(camera,
                                                   viewportMin,
                                                   viewportMax,
                                                   mousePos,
                                                   gizmoState.DragStartGizmoOrigin,
                                                   gizmoState.DragPlaneNormal,
                                                   currentMouseWorld))
                    {
                        glm::vec3 delta = currentMouseWorld - gizmoState.DragStartWorldPosition;
                        if (gizmoState.DragAxis >= 0 && gizmoState.DragAxis < 3)
                        {
                            const glm::vec3 axisDirection = GetGizmoAxisDirection(gizmoState.DragAxis);
                            delta = axisDirection * glm::dot(delta, axisDirection);
                        }

                        transform->Position = gizmoState.DragStartEntityPosition + delta;
                        scene.MarkTransformDirty(selectedEntity);

                        // Apply same delta to multi-selected entities
                        for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                        {
                            entt::entity e = gizmoState.DragEntities[i];
                            if (!scene.IsValid(e)) continue;
                            auto* t = registry.try_get<TransformComponent>(e);
                            if (t)
                            {
                                t->Position = gizmoState.DragStartPositions[i] + delta;
                                scene.MarkTransformDirty(e);
                            }
                        }
                    }

                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        if (undoService)
                            (void)undoService->CommitInteractiveSceneMutation("Move Entity");
                        gizmoState.DragActive = false;
                    }
                }
            }
            // ---- ROTATE GIZMO (3-axis) ----
            else if (gizmoState.Mode == TransformGizmoMode::Rotate)
            {
                constexpr int kRingSegments = 64;
                const float worldRadius = kGizmoRotateRadius / pixelsPerUnit;

                // Project a world-space circle (ring) to screen and draw it.
                // axis: 0=X (ring in YZ plane), 1=Y (ring in XZ plane), 2=Z (ring in XY plane)
                auto projectAndDrawRing = [&](int axis, ImU32 color, float thickness,
                                              std::array<ImVec2, kRingSegments>& outScreenPoints,
                                              int& outValidCount)
                {
                    outValidCount = 0;
                    ImVec2 prevScreen{};
                    bool prevValid = false;
                    for (int seg = 0; seg <= kRingSegments; ++seg)
                    {
                        const float angle = (static_cast<float>(seg % kRingSegments) / static_cast<float>(kRingSegments)) * 6.2831853f;
                        const float ca = std::cos(angle);
                        const float sa = std::sin(angle);
                        glm::vec3 worldPt = entityWorldPos;
                        if (axis == 0) { worldPt.y += ca * worldRadius; worldPt.z += sa * worldRadius; }
                        else if (axis == 1) { worldPt.x += ca * worldRadius; worldPt.z += sa * worldRadius; }
                        else { worldPt.x += ca * worldRadius; worldPt.y += sa * worldRadius; }

                        ImVec2 screenPt{};
                        const bool valid = WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, worldPt, screenPt);
                        if (valid && seg < kRingSegments)
                        {
                            outScreenPoints[outValidCount] = screenPt;
                            outValidCount++;
                        }
                        if (valid && prevValid && seg > 0)
                            drawList->AddLine(prevScreen, screenPt, color, thickness);
                        prevScreen = screenPt;
                        prevValid = valid;
                    }
                };

                // Compute closest distance from mouse to a ring's projected points
                auto ringDistanceToMouse = [&](const std::array<ImVec2, kRingSegments>& pts, int count) -> float
                {
                    float minDist = std::numeric_limits<float>::max();
                    for (int i = 0; i < count; ++i)
                    {
                        const int j = (i + 1) % count;
                        const ImVec2& a = pts[i];
                        const ImVec2& b = pts[j];
                        const float abx = b.x - a.x;
                        const float aby = b.y - a.y;
                        const float lenSq = abx * abx + aby * aby;
                        float t = 0.0f;
                        if (lenSq > 0.001f)
                            t = std::clamp(((mousePos.x - a.x) * abx + (mousePos.y - a.y) * aby) / lenSq, 0.0f, 1.0f);
                        const float cx = a.x + t * abx;
                        const float cy = a.y + t * aby;
                        const float dist = std::sqrt((mousePos.x - cx) * (mousePos.x - cx) + (mousePos.y - cy) * (mousePos.y - cy));
                        minDist = std::min(minDist, dist);
                    }
                    return minDist;
                };

                // Draw all 3 rings
                constexpr ImU32 kRingColors[3] = { kGizmoColorX, kGizmoColorY, kGizmoColorZ };
                const char* kAxisLabels[3] = { "X", "Y", "Z" };
                std::array<ImVec2, kRingSegments> ringPts[3]{};
                int ringCounts[3] = {};

                for (int axis = 0; axis < 3; ++axis)
                {
                    const ImU32 color = (activeAxis == axis) ? kGizmoColorActive : kRingColors[axis];
                    const float thickness = (activeAxis == axis) ? 3.0f : 2.0f;
                    projectAndDrawRing(axis, color, thickness, ringPts[axis], ringCounts[axis]);
                }

                // Draw angle indicator line for each axis
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float angleRad = glm::radians(transform->Rotation[axis]);
                    const float ca = std::cos(angleRad);
                    const float sa = std::sin(angleRad);
                    glm::vec3 indicatorWorld = entityWorldPos;
                    if (axis == 0) { indicatorWorld.y += ca * worldRadius; indicatorWorld.z += sa * worldRadius; }
                    else if (axis == 1) { indicatorWorld.x += ca * worldRadius; indicatorWorld.z += sa * worldRadius; }
                    else { indicatorWorld.x += ca * worldRadius; indicatorWorld.y += sa * worldRadius; }

                    ImVec2 indicatorScreen{};
                    if (WorldToViewportPoint(camera, viewportMin, viewportWidth, viewportHeight, indicatorWorld, indicatorScreen))
                    {
                        drawList->AddLine(originScreen, indicatorScreen,
                            (activeAxis == axis) ? kGizmoColorActive : kRingColors[axis], 1.2f);
                        drawList->AddCircleFilled(indicatorScreen, 3.0f, kRingColors[axis]);
                    }
                }

                // Center dot
                drawList->AddCircleFilled(originScreen, 3.0f, IM_COL32(200, 200, 200, 200));

                // Hit test all 3 rings — pick the closest one within threshold
                int hoveredAxis = -1;
                if (canEdit && mouseInViewport && !gizmoState.DragActive)
                {
                    float bestDist = kGizmoHitRadius + 1.0f;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (ringCounts[axis] < 3) continue;
                        const float dist = ringDistanceToMouse(ringPts[axis], ringCounts[axis]);
                        if (dist <= kGizmoHitRadius && dist < bestDist)
                        {
                            bestDist = dist;
                            hoveredAxis = axis;
                        }
                    }
                }

                if (hoveredAxis >= 0)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                // Begin drag
                if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        undoService->BeginInteractiveSceneMutation();
                    gizmoState.DragActive = true;
                    gizmoState.DragAxis = hoveredAxis;
                    gizmoState.DragEntity = selectedEntity;
                    gizmoState.DragStartEntityRotation = transform->Rotation;
                    const float dx = mousePos.x - originScreen.x;
                    const float dy = -(mousePos.y - originScreen.y);
                    gizmoState.DragStartAngle = std::atan2(dy, dx);

                    // Snapshot multi-selection rotations
                    gizmoState.DragEntities.clear();
                    gizmoState.DragStartRotations.clear();
                    for (entt::entity e : multiSelectedEntities)
                    {
                        if (e != selectedEntity && scene.IsValid(e))
                        {
                            auto* t = registry.try_get<TransformComponent>(e);
                            if (t)
                            {
                                gizmoState.DragEntities.push_back(e);
                                gizmoState.DragStartRotations.push_back(t->Rotation);
                            }
                        }
                    }
                }

                // Continue drag
                if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                    gizmoState.Mode == TransformGizmoMode::Rotate)
                {
                    const float dx = mousePos.x - originScreen.x;
                    const float dy = -(mousePos.y - originScreen.y);
                    const float currentAngle = std::atan2(dy, dx);
                    const float angleDelta = glm::degrees(currentAngle - gizmoState.DragStartAngle);

                    const int dragAxis = gizmoState.DragAxis;
                    transform->Rotation[dragAxis] = gizmoState.DragStartEntityRotation[dragAxis] + angleDelta;
                    scene.MarkTransformDirty(selectedEntity);

                    // Apply same rotation delta to multi-selected
                    for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                    {
                        entt::entity e = gizmoState.DragEntities[i];
                        if (!scene.IsValid(e)) continue;
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            t->Rotation[dragAxis] = gizmoState.DragStartRotations[i][dragAxis] + angleDelta;
                            scene.MarkTransformDirty(e);
                        }
                    }

                    // Draw angle feedback
                    char angleBuf[48]{};
                    std::snprintf(angleBuf, sizeof(angleBuf), "%s: %.1f deg", kAxisLabels[dragAxis], angleDelta);
                    drawList->AddText(ImVec2(originScreen.x + kGizmoRotateRadius + 12.0f, originScreen.y - 10.0f),
                                     kRingColors[dragAxis], angleBuf);

                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        if (undoService)
                            (void)undoService->CommitInteractiveSceneMutation("Rotate Entity");
                        gizmoState.DragActive = false;
                    }
                }
            }
            // ---- SCALE GIZMO ----
            else if (gizmoState.Mode == TransformGizmoMode::Scale)
            {
                // Draw axes
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!axisVisible[axis] || axisScreenLengths[axis] <= 1.0f)
                        continue;

                    drawList->AddLine(originScreen, axisEnds[axis], AxisColor(axis, activeAxis), 2.5f);
                }

                // Draw scale boxes at endpoints
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                        continue;

                    drawList->AddRectFilled(
                        ImVec2(axisEnds[axis].x - kGizmoScaleBoxSize, axisEnds[axis].y - kGizmoScaleBoxSize),
                        ImVec2(axisEnds[axis].x + kGizmoScaleBoxSize, axisEnds[axis].y + kGizmoScaleBoxSize),
                        AxisColor(axis, activeAxis));
                }

                // Center uniform scale handle
                drawList->AddRectFilled(
                    ImVec2(originScreen.x - kGizmoScaleBoxSize, originScreen.y - kGizmoScaleBoxSize),
                    ImVec2(originScreen.x + kGizmoScaleBoxSize, originScreen.y + kGizmoScaleBoxSize),
                    activeAxis == 3 ? kGizmoColorActive : IM_COL32(200, 200, 200, 220));

                // Hit test
                int hoveredAxis = -1;
                if (canEdit && mouseInViewport && !gizmoState.DragActive)
                {
                    // Center (uniform scale, axis=3)
                    if (IsMouseNearPoint(mousePos, originScreen, kGizmoScaleBoxSize + 4.0f))
                        hoveredAxis = 3;

                    if (hoveredAxis < 0)
                    {
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (!axisVisible[axis] || axisScreenLengths[axis] <= 8.0f)
                                continue;
                            if (!IsMouseNearPoint(mousePos, axisEnds[axis], kGizmoScaleBoxSize + 4.0f))
                                continue;

                            hoveredAxis = axis;
                            break;
                        }
                    }
                }

                if (hoveredAxis >= 0)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                // Begin drag
                if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (undoService)
                        undoService->BeginInteractiveSceneMutation();
                    gizmoState.DragActive = true;
                    gizmoState.DragAxis = hoveredAxis;
                    gizmoState.DragEntity = selectedEntity;
                    gizmoState.DragStartEntityScale = transform->Scale;
                    gizmoState.DragStartGizmoOrigin = entityWorldPos;
                    gizmoState.DragStartMousePosition = glm::vec2(mousePos.x, mousePos.y);

                    if (hoveredAxis == 3)
                    {
                        gizmoState.DragPlaneNormal = GetCameraForwardDirection(camera);
                        gizmoState.DragStartWorldPosition = gizmoState.DragStartGizmoOrigin;
                        const float dx = mousePos.x - originScreen.x;
                        const float dy = mousePos.y - originScreen.y;
                        gizmoState.DragReferenceValue = std::sqrt(dx * dx + dy * dy);
                    }
                    else
                    {
                        const glm::vec3 axisDirection = GetGizmoAxisDirection(hoveredAxis);
                        gizmoState.DragPlaneNormal = ComputeAxisDragPlaneNormal(camera, axisDirection, entityWorldPos);
                        gizmoState.DragStartWorldPosition = gizmoState.DragStartGizmoOrigin + axisDirection * worldAxisLength;
                        gizmoState.DragReferenceValue = worldAxisLength;
                    }

                    // Snapshot multi-selection scales
                    gizmoState.DragEntities.clear();
                    gizmoState.DragStartScales.clear();
                    for (entt::entity e : multiSelectedEntities)
                    {
                        if (e != selectedEntity && scene.IsValid(e))
                        {
                            auto* t = registry.try_get<TransformComponent>(e);
                            if (t)
                            {
                                gizmoState.DragEntities.push_back(e);
                                gizmoState.DragStartScales.push_back(t->Scale);
                            }
                        }
                    }
                }

                // Continue drag
                if (gizmoState.DragActive && gizmoState.DragEntity == selectedEntity &&
                    gizmoState.Mode == TransformGizmoMode::Scale)
                {
                    glm::vec3 scaleFactor(1.0f);
                    if (gizmoState.DragAxis == 3)
                    {
                        const float startDistance = gizmoState.DragReferenceValue;
                        float uniformFactor = 1.0f;
                        if (startDistance > 8.0f)
                        {
                            const float dx = mousePos.x - originScreen.x;
                            const float dy = mousePos.y - originScreen.y;
                            const float currentDistance = std::sqrt(dx * dx + dy * dy);
                            uniformFactor = currentDistance / startDistance;
                        }
                        else
                        {
                            const float deltaPixels = (mousePos.x - gizmoState.DragStartMousePosition.x) -
                                                      (mousePos.y - gizmoState.DragStartMousePosition.y);
                            uniformFactor = 1.0f + deltaPixels * 0.01f;
                        }
                        scaleFactor = glm::vec3(std::max(uniformFactor, 0.001f));
                    }
                    else
                    {
                        glm::vec3 currentMouseWorld{};
                        if (TryIntersectMouseWithPlane(camera,
                                                       viewportMin,
                                                       viewportMax,
                                                       mousePos,
                                                       gizmoState.DragStartGizmoOrigin,
                                                       gizmoState.DragPlaneNormal,
                                                       currentMouseWorld))
                        {
                            const glm::vec3 axisDirection = GetGizmoAxisDirection(gizmoState.DragAxis);
                            const float referenceDistance = std::abs(gizmoState.DragReferenceValue) > 0.001f
                                ? gizmoState.DragReferenceValue
                                : 1.0f;
                            const float currentAxisDistance = glm::dot(currentMouseWorld - gizmoState.DragStartGizmoOrigin,
                                                                       axisDirection);
                            scaleFactor[gizmoState.DragAxis] = currentAxisDistance / referenceDistance;
                        }
                    }

                    transform->Scale = gizmoState.DragStartEntityScale * scaleFactor;
                    scene.MarkTransformDirty(selectedEntity);

                    for (size_t i = 0; i < gizmoState.DragEntities.size(); ++i)
                    {
                        entt::entity e = gizmoState.DragEntities[i];
                        if (!scene.IsValid(e)) continue;
                        auto* t = registry.try_get<TransformComponent>(e);
                        if (t)
                        {
                            t->Scale = gizmoState.DragStartScales[i] * scaleFactor;
                            scene.MarkTransformDirty(e);
                        }
                    }

                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        if (undoService)
                            (void)undoService->CommitInteractiveSceneMutation("Scale Entity");
                        gizmoState.DragActive = false;
                    }
                }
            }

            // Cancel on Escape
            if (gizmoState.DragActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                if (undoService)
                    undoService->CancelInteractiveSceneMutation();
                gizmoState.DragActive = false;
            }

            // Abort if mouse released unexpectedly
            if (gizmoState.DragActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (undoService)
                    (void)undoService->CommitInteractiveSceneMutation("Transform Entity");
                gizmoState.DragActive = false;
            }

            return gizmoState.DragActive;
        }

        // -----------------------------------------------------------------
        // Keyboard shortcuts for gizmo mode switching (Unity-style W/E/R/Q)
        // -----------------------------------------------------------------

        void HandleGizmoKeyboardShortcuts(TransformGizmoState& gizmoState, bool viewportFocused)
        {
            if (!viewportFocused)
                return;
            if (gizmoState.DragActive)
                return;

            // Don't capture shortcuts when typing in an input field
            if (ImGui::GetIO().WantTextInput)
                return;

            if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
                gizmoState.Mode = TransformGizmoMode::None;
            else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                gizmoState.Mode = TransformGizmoMode::Translate;
            else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                gizmoState.Mode = TransformGizmoMode::Rotate;
            else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                gizmoState.Mode = TransformGizmoMode::Scale;
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
            // Don't pick if a gizmo drag just started or is active
            if (gizmoState && gizmoState->DragActive)
                return;

            const ImVec2 mousePos = ImGui::GetMousePos();
            if (mousePos.x < viewportMin.x || mousePos.x > viewportMax.x ||
                mousePos.y < viewportMin.y || mousePos.y > viewportMax.y)
                return;

            // Don't pick if right mouse button is held (camera control)
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
                    // Toggle selection
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
                    // Single select
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
                // Clicked empty space: deselect
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

            // Don't start box select during gizmo drag or RMB look
            if (gizmoState->DragActive)
                return;
            if (ImGui::GetIO().MouseDown[ImGuiMouseButton_Right])
            {
                gizmoState->BoxSelectActive = false;
                return;
            }

            const ImVec2 mousePos = ImGui::GetMousePos();

            // Begin box select on left mouse down while holding no gizmo
            if (sceneViewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !gizmoState->DragActive)
            {
                // Only start box select if we didn't hit a gizmo handle or an entity
                // (picking and gizmo begin happen before this, so if DragActive is false
                //  and we got here, it means no gizmo was engaged this frame)
                // We use a delayed approach: mark start position, and only activate
                // box select if the mouse moves a minimum distance.
                gizmoState->BoxSelectStart = glm::vec2(mousePos.x, mousePos.y);
            }

            // Activate box select after minimum drag distance (only while hovering viewport)
            if (!gizmoState->BoxSelectActive && sceneViewHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float dx = mousePos.x - gizmoState->BoxSelectStart.x;
                const float dy = mousePos.y - gizmoState->BoxSelectStart.y;
                if (dx * dx + dy * dy > 25.0f) // 5px threshold
                    gizmoState->BoxSelectActive = true;
            }

            // Draw box and compute selection on release
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

                    // Find all sprite entities whose centers fall within the box
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
                        // Skip UI entities that live under a Canvas.
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

            // Cancel on escape or right click
            if (gizmoState->BoxSelectActive && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
                gizmoState->BoxSelectActive = false;
        }

        // -----------------------------------------------------------------
        // Gizmo mode toolbar overlay
        // -----------------------------------------------------------------

        void DrawGizmoToolbar(ImDrawList* drawList,
                              const ImVec2& viewportMin,
                              const ImVec2& viewportMax,
                              TransformGizmoState& gizmoState)
        {
            (void)viewportMax;
            const float buttonSize = 28.0f;
            const float padding = 4.0f;
            const float toolbarWidth = buttonSize * 4 + padding * 5;
            const float toolbarHeight = buttonSize + padding * 2;
            const ImVec2 toolbarMin(viewportMin.x + 10.0f, viewportMin.y + 10.0f);
            const ImVec2 toolbarMax(toolbarMin.x + toolbarWidth, toolbarMin.y + toolbarHeight);

            drawList->AddRectFilled(toolbarMin, toolbarMax, IM_COL32(30, 30, 35, 210), 5.0f);
            drawList->AddRect(toolbarMin, toolbarMax, IM_COL32(255, 255, 255, 30), 5.0f);

            struct ToolButton
            {
                const char* Label;
                const char* Shortcut;
                TransformGizmoMode Mode;
            };

            const ToolButton buttons[] = {
                {"Q", "Q", TransformGizmoMode::None},
                {"W", "W", TransformGizmoMode::Translate},
                {"E", "E", TransformGizmoMode::Rotate},
                {"R", "R", TransformGizmoMode::Scale},
            };

            for (int i = 0; i < 4; ++i)
            {
                const float x = toolbarMin.x + padding + static_cast<float>(i) * (buttonSize + padding);
                const float y = toolbarMin.y + padding;
                const ImVec2 btnMin(x, y);
                const ImVec2 btnMax(x + buttonSize, y + buttonSize);

                const bool isActive = gizmoState.Mode == buttons[i].Mode;
                const ImU32 btnColor = isActive ? IM_COL32(80, 140, 220, 220) : IM_COL32(55, 55, 60, 180);
                const ImU32 btnHoverColor = isActive ? IM_COL32(90, 155, 240, 240) : IM_COL32(70, 70, 78, 200);

                const ImVec2 mousePos = ImGui::GetMousePos();
                const bool hovered = mousePos.x >= btnMin.x && mousePos.x <= btnMax.x &&
                                     mousePos.y >= btnMin.y && mousePos.y <= btnMax.y;

                drawList->AddRectFilled(btnMin, btnMax, hovered ? btnHoverColor : btnColor, 3.0f);
                if (isActive)
                    drawList->AddRect(btnMin, btnMax, IM_COL32(130, 190, 255, 255), 3.0f, 0, 1.5f);

                const ImVec2 textSize = ImGui::CalcTextSize(buttons[i].Label);
                drawList->AddText(
                    ImVec2(btnMin.x + (buttonSize - textSize.x) * 0.5f,
                           btnMin.y + (buttonSize - textSize.y) * 0.5f),
                    IM_COL32(235, 240, 250, 255),
                    buttons[i].Label);

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    gizmoState.Mode = buttons[i].Mode;
            }
        }

        bool DrawSelectedPhysicsOverlays(ImDrawList* drawList,
                                         Scene& scene,
                                         const Camera& camera,
                                         entt::entity selectedEntity,
                                         const ImVec2& viewportMin,
                                         const ImVec2& viewportMax,
                                         float viewportWidth,
                                         float viewportHeight,
                                         EditorPlayModeState playModeState,
                                         EditorUndoService* undoService)
        {
            const bool colliderCapturedInput = DrawAndHandleColliderGizmos(
                drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
            const bool lightingCapturedInput = !colliderCapturedInput && DrawAndHandleLightingGizmos(
                drawList, scene, camera, selectedEntity, viewportMin, viewportMax, viewportWidth, viewportHeight, playModeState, undoService);
            return colliderCapturedInput || lightingCapturedInput;
        }
    }

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

        auto sanitizeViewportDimension = [](float value) -> uint32_t {
            if (!std::isfinite(value) || value <= 1.0f)
                return 0;
            return static_cast<uint32_t>(std::floor(value));
        };

        auto drawLoadingOverlay = [&scene](const ImVec2& minPos, const ImVec2& maxPos) -> bool {
            const LoadingScreen::Context ctx = LoadingScreen::BuildContext(
                scene, Renderer2D::Default().IsShaderReady(), Renderer2D::GetDefaultShaderKey());
            const LoadingScreen::State state = LoadingScreen::GetState(ctx);

            static bool s_LoadingSessionActive = false;
            static bool s_LoadingToastVisible = false;
            static double s_LoadingVisibleWindowStartTime = 0.0;
            static double s_LoadingToastHideDeadline = 0.0;
            static std::string s_LastLoadingStatusText = "Loading...";
            static float s_LastLoadingProgressValue = 0.0f;
            constexpr double kLoadingToastDelaySeconds = 0.2;
            constexpr double kLoadingToastHoldSeconds = 0.8;
            const double nowSeconds = ImGui::GetTime();

            if (state.IsLoading)
            {
                if (!s_LoadingSessionActive)
                {
                    s_LoadingSessionActive = true;
                    s_LoadingVisibleWindowStartTime = nowSeconds;
                    s_LoadingToastVisible = false;
                }

                s_LastLoadingStatusText = state.StatusText.empty() ? "Loading..." : state.StatusText;
                s_LastLoadingProgressValue = std::clamp(state.Progress, 0.0f, 1.0f);

                if (!s_LoadingToastVisible &&
                    (nowSeconds - s_LoadingVisibleWindowStartTime) >= kLoadingToastDelaySeconds)
                {
                    s_LoadingToastVisible = true;
                }
            }
            else
            {
                if (s_LoadingSessionActive)
                {
                    s_LoadingSessionActive = false;
                    // Always show a short completion toast, even for very fast
                    // loads that finished before the initial show-delay elapsed.
                    s_LoadingToastVisible = true;
                    std::string completionStatus = s_LastLoadingStatusText;
                    if (completionStatus.empty())
                        completionStatus = "Loading";
                    if (completionStatus.size() >= 3 &&
                        completionStatus.substr(completionStatus.size() - 3) == "...")
                    {
                        completionStatus = completionStatus.substr(0, completionStatus.size() - 3);
                    }
                    s_LastLoadingStatusText = completionStatus + " complete";
                    s_LastLoadingProgressValue = 1.0f;
                    s_LoadingToastHideDeadline = nowSeconds + kLoadingToastHoldSeconds;
                }

                if (s_LoadingToastVisible && nowSeconds >= s_LoadingToastHideDeadline)
                {
                    s_LoadingToastVisible = false;
                }
            }

            if (!s_LoadingToastVisible)
                return false;

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            const char* loadingText = s_LastLoadingStatusText.c_str();
            const float progressValue = std::clamp(s_LastLoadingProgressValue, 0.0f, 1.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(loadingText);

            const float margin = 12.0f;
            const float panelWidth = std::max(260.0f, textSize.x + 28.0f);
            const float panelHeight = 54.0f;

            ImVec2 panelMin(maxPos.x - panelWidth - margin, maxPos.y - panelHeight - margin);
            panelMin.x = std::max(panelMin.x, minPos.x + margin);
            panelMin.y = std::max(panelMin.y, minPos.y + margin);
            ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);
            panelMax.x = std::min(panelMax.x, maxPos.x - margin);
            panelMax.y = std::min(panelMax.y, maxPos.y - margin);

            drawList->AddRectFilled(panelMin, panelMax, IM_COL32(24, 24, 28, 215), 6.0f);
            drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 32), 6.0f);

            drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 9.0f),
                              IM_COL32(235, 235, 240, 255),
                              loadingText);

            const float barHeight = 6.0f;
            const ImVec2 barMin(panelMin.x + 12.0f, panelMax.y - 14.0f);
            const ImVec2 barMax(panelMax.x - 12.0f, barMin.y + barHeight);
            drawList->AddRectFilled(barMin, barMax, IM_COL32(58, 58, 64, 230), 3.0f);
            const ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * progressValue, barMax.y);
            drawList->AddRectFilled(barMin, fillMax, IM_COL32(95, 160, 245, 255), 3.0f);
            return true;
        };

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (showSceneView)
        {
            if (focusSceneViewRequested)
            {
                ImGui::SetNextWindowFocus();
                focusSceneViewRequested = false;
            }
            const bool sceneWindowVisible = ImGui::Begin("Scene View", &showSceneView);

            sceneViewFocused = ImGui::IsWindowFocused();
            sceneViewHovered = ImGui::IsWindowHovered();
            const bool skipSceneRender = !sceneWindowVisible || ImGui::IsWindowCollapsed();
            const ImVec2 sceneViewSize = ImGui::GetContentRegionAvail();
            const uint32_t sceneWidth = sanitizeViewportDimension(sceneViewSize.x);
            const uint32_t sceneHeight = sanitizeViewportDimension(sceneViewSize.y);

            if (!skipSceneRender && sceneWidth > 0 && sceneHeight > 0)
            {
                ensureSceneViewFramebuffer(sceneWidth, sceneHeight);
                sceneViewWidthPixels = sceneWidth;
                sceneViewHeightPixels = sceneHeight;

                if (sceneViewCamera)
                    sceneViewCamera->SetViewportSize(sceneWidth, sceneHeight);

                const bool isSceneLoading = scene && scene->GetLoadState() == Scene::LoadState::Loading;
                if (sceneViewCamera && scene && sceneViewFramebuffer && !isSceneLoading)
                    SceneRenderer::RenderToViewport(*scene, *sceneViewCamera, sceneViewFramebuffer, sceneWidth, sceneHeight);

                if (sceneViewFramebuffer && sceneViewFramebuffer->GetColorAttachment())
                {
                    ImGui::Image(
                        static_cast<ImTextureID>(GetTextureNativeHandle(sceneViewFramebuffer->GetColorAttachment())),
                        ImVec2(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)),
                        ImVec2(0, 1),
                        ImVec2(1, 0));
                    const ImVec2 sceneRectMin = ImGui::GetItemRectMin();
                    const ImVec2 sceneRectMax = ImGui::GetItemRectMax();
                    sceneViewRectValid = true;
                    sceneViewRectMinPixels = glm::vec2(sceneRectMin.x, sceneRectMin.y);
                    sceneViewRectMaxPixels = glm::vec2(sceneRectMax.x, sceneRectMax.y);

                    if (scene && sceneViewCamera && !isSceneLoading)
                    {
                        const ImVec2 viewportMin = sceneRectMin;
                        const ImVec2 viewportMax = sceneRectMax;
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        const bool physicsOverlayCapturedInput = DrawSelectedPhysicsOverlays(drawList,
                                                                                             *scene,
                                                                                             *sceneViewCamera,
                                                                                             selectedEntity,
                                                                                             viewportMin,
                                                                                             viewportMax,
                                                                                             static_cast<float>(sceneWidth),
                                                                                             static_cast<float>(sceneHeight),
                                                                                             playModeState,
                                                                                             undoService);
                        if (tilemapEditorState)
                        {
                            auto& reg = scene->GetRegistry();
                            entt::entity gridEntity = tilemapEditorState->ActiveGridEntity;
                            entt::entity layerEntity = tilemapEditorState->ActiveLayerEntity;

                            const bool preferredTargetsValid =
                                gridEntity != entt::null &&
                                layerEntity != entt::null &&
                                scene->IsValid(gridEntity) &&
                                scene->IsValid(layerEntity) &&
                                reg.all_of<Grid2DComponent>(gridEntity) &&
                                reg.all_of<TilemapLayerComponent>(layerEntity);

                            if (!preferredTargetsValid && reg.all_of<Grid2DComponent>(selectedEntity))
                            {
                                gridEntity = selectedEntity;
                                for (entt::entity child : scene->GetChildren(selectedEntity))
                                {
                                    if (reg.all_of<TilemapLayerComponent>(child))
                                    {
                                        layerEntity = child;
                                        break;
                                    }
                                }
                            }
                            else if (!preferredTargetsValid && reg.all_of<TilemapLayerComponent>(selectedEntity))
                            {
                                layerEntity = selectedEntity;
                                entt::entity parent = scene->GetParent(selectedEntity);
                                if (parent != entt::null && scene->IsValid(parent) && reg.all_of<Grid2DComponent>(parent))
                                    gridEntity = parent;
                            }

                            if (gridEntity != entt::null && layerEntity != entt::null)
                            {
                                (void)DrawAndHandleGrid2DEditing(drawList,
                                    *scene,
                                    *sceneViewCamera,
                                    gridEntity,
                                    layerEntity,
                                    viewportMin,
                                    viewportMax,
                                    static_cast<float>(sceneWidth),
                                    static_cast<float>(sceneHeight),
                                    playModeState,
                                    undoService,
                                    *tilemapEditorState,
                                    std::string{});
                            }
                        }

                        if (scenePanelState)
                        {
                            for (entt::entity entity : scenePanelState->MultiSelectedEntities)
                            {
                                const ImU32 highlightColor = (entity == selectedEntity)
                                    ? IM_COL32(255, 180, 50, 220)
                                    : IM_COL32(100, 180, 255, 180);
                                DrawSelectionHighlight(drawList, *scene, *sceneViewCamera, entity,
                                                       viewportMin, static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                       highlightColor);
                            }
                        }
                        else if (selectedEntity != entt::null)
                        {
                            DrawSelectionHighlight(drawList, *scene, *sceneViewCamera, selectedEntity,
                                                   viewportMin, static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                   IM_COL32(255, 180, 50, 220));
                        }

                        bool gizmoCapturedInput = physicsOverlayCapturedInput;
                        if (gizmoState && !physicsOverlayCapturedInput)
                        {
                            static const std::vector<entt::entity> kEmptyEntities;
                            const std::vector<entt::entity>& multiEntities = scenePanelState
                                ? scenePanelState->MultiSelectedEntities
                                : kEmptyEntities;
                            gizmoCapturedInput = DrawAndHandleTransformGizmos(drawList,
                                *scene,
                                *sceneViewCamera,
                                selectedEntity,
                                multiEntities,
                                viewportMin,
                                viewportMax,
                                static_cast<float>(sceneWidth),
                                static_cast<float>(sceneHeight),
                                playModeState,
                                undoService,
                                *gizmoState) || gizmoCapturedInput;
                        }

                        if (!gizmoCapturedInput && scene && sceneViewCamera)
                        {
                            HandleSceneViewPicking(*scene, *sceneViewCamera, selectedEntity, scenePanelState,
                                                   viewportMin, viewportMax,
                                                   static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                   sceneViewHovered, gizmoState);
                        }

                        if (!gizmoCapturedInput && scene && sceneViewCamera)
                        {
                            HandleBoxSelection(drawList, *scene, *sceneViewCamera, selectedEntity, scenePanelState,
                                               viewportMin, viewportMax,
                                               static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                               sceneViewHovered, gizmoState);
                        }

                        if (gizmoState && showGizmoToolbar)
                        {
                            const ImVec2 toolbarViewportMin = showFpsOverlay
                                ? ImVec2(viewportMin.x, viewportMin.y + 145.0f)
                                : viewportMin;
                            DrawGizmoToolbar(drawList, toolbarViewportMin, viewportMax, *gizmoState);
                        }
                    }

                    if (gizmoState)
                        HandleGizmoKeyboardShortcuts(*gizmoState, sceneViewFocused || sceneViewHovered);

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(scenePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0] && onSceneDropped)
                            {
                                onSceneDropped(key);
                                scene = nullptr;
                            }
                        }
                        if (prefabPayloadId)
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(prefabPayloadId))
                            {
                                std::string key;
                                if (payload->Data && payload->DataSize > 0)
                                {
                                    const auto* keyChars = static_cast<const char*>(payload->Data);
                                    const int keyLength = std::max(0, payload->DataSize - 1);
                                    key.assign(keyChars, keyChars + keyLength);
                                }
                                if (!key.empty() && onPrefabDropped)
                                {
                                    glm::vec3 worldPosition(0.0f);
                                    if (sceneViewCamera)
                                    {
                                        const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                        const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                        const ImVec2 mousePos = ImGui::GetMousePos();
                                        if (!TryComputeDropWorldPosition(*sceneViewCamera, viewportMin, viewportMax, mousePos, worldPosition))
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
                                if (key && key[0] && scene && sceneViewCamera)
                                {
                                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                    const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                    const ImVec2 mousePos = ImGui::GetMousePos();

                                    entt::entity targetEntity = entt::null;
                                    if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                        mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                                    {
                                        const float viewportWidth = viewportMax.x - viewportMin.x;
                                        const float viewportHeight = viewportMax.y - viewportMin.y;
                                        const auto picked = PickTopmostSpriteEntityAtPoint(*scene, *sceneViewCamera, viewportMin, viewportWidth, viewportHeight, mousePos);
                                        if (picked.has_value())
                                            targetEntity = *picked;
                                    }

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
                        if (assetMovePayloadId)
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                            {
                                std::string assetKey;
                                if (payload->Data && payload->DataSize > 0)
                                {
                                    const auto* keyChars = static_cast<const char*>(payload->Data);
                                    const int keyLength = std::max(0, payload->DataSize - 1);
                                    assetKey.assign(keyChars, keyChars + keyLength);
                                }

                                if (!assetKey.empty() && scene && sceneViewCamera)
                                {
                                    const ImVec2 viewportMin = ImGui::GetItemRectMin();
                                    const ImVec2 viewportMax = ImGui::GetItemRectMax();
                                    const ImVec2 mousePos = ImGui::GetMousePos();

                                    entt::entity targetEntity = entt::null;
                                    if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                                        mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                                    {
                                        const float viewportWidth = viewportMax.x - viewportMin.x;
                                        const float viewportHeight = viewportMax.y - viewportMin.y;
                                        const auto picked = PickTopmostSpriteEntityAtPoint(*scene, *sceneViewCamera, viewportMin, viewportWidth, viewportHeight, mousePos);
                                        if (picked.has_value())
                                            targetEntity = *picked;
                                    }

                                    if (targetEntity == entt::null && selectedEntity != entt::null && scene->IsValid(selectedEntity))
                                        targetEntity = selectedEntity;

                                    if (targetEntity != entt::null && scene->IsValid(targetEntity) &&
                                        TryAttachScriptAssetToEntity(scene, targetEntity, assetKey, undoService))
                                    {
                                        selectedEntity = targetEntity;
                                        selectedTextureAssetKey.clear();
                                        cachedTextureAsset.reset();
                                        selectedMaterialAssetKey.clear();
                                        cachedMaterialAsset.reset();
                                        selectedNativeScriptAssetKey.clear();
                                    }
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    const bool sceneLoadingToastDrawn = drawLoadingOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    if (!sceneLoadingToastDrawn && !sceneViewCamera)
                    {
                        const ImVec2 minPos = ImGui::GetItemRectMin();
                        const ImVec2 maxPos = ImGui::GetItemRectMax();
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

                        const char* text = "Scene View: Editor camera is unavailable.";
                        const ImVec2 textSize = ImGui::CalcTextSize(text);
                        const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                        drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                    }

                    if (showFpsOverlay)
                    {
                        const ImVec2 minPos = ImGui::GetItemRectMin();
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        struct FpsOverlayHistory
                        {
                            std::array<float, 180> FrameTimesMs{};
                            size_t NextIndex = 0;
                            size_t SampleCount = 0;
                        };
                        static FpsOverlayHistory fpsHistory{};

                        const float deltaTimeMs = std::max(0.0f, ImGui::GetIO().DeltaTime * 1000.0f);
                        if (deltaTimeMs > 0.0f)
                        {
                            fpsHistory.FrameTimesMs[fpsHistory.NextIndex] = deltaTimeMs;
                            fpsHistory.NextIndex = (fpsHistory.NextIndex + 1) % fpsHistory.FrameTimesMs.size();
                            fpsHistory.SampleCount = std::min(fpsHistory.SampleCount + 1, fpsHistory.FrameTimesMs.size());
                        }

                        float minFrameMs = 0.0f;
                        float maxFrameMs = 0.0f;
                        float avgFrameMs = 0.0f;
                        if (fpsHistory.SampleCount > 0)
                        {
                            minFrameMs = std::numeric_limits<float>::max();
                            for (size_t sampleIndex = 0; sampleIndex < fpsHistory.SampleCount; ++sampleIndex)
                            {
                                const size_t readIndex =
                                    (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount + sampleIndex) %
                                    fpsHistory.FrameTimesMs.size();
                                const float sampleMs = fpsHistory.FrameTimesMs[readIndex];
                                minFrameMs = std::min(minFrameMs, sampleMs);
                                maxFrameMs = std::max(maxFrameMs, sampleMs);
                                avgFrameMs += sampleMs;
                            }
                            avgFrameMs /= static_cast<float>(fpsHistory.SampleCount);
                        }

                        const float fps = (deltaTimeMs > 0.01f) ? (1000.0f / deltaTimeMs) : ImGui::GetIO().Framerate;
                        const ImU32 statusColor = (deltaTimeMs <= 16.67f)
                            ? IM_COL32(120, 255, 130, 255)
                            : ((deltaTimeMs <= 33.33f) ? IM_COL32(255, 220, 100, 255) : IM_COL32(255, 120, 120, 255));

                        const ImVec2 panelMin(minPos.x + 10.0f, minPos.y + 10.0f);
                        const ImVec2 panelMax(panelMin.x + 280.0f, panelMin.y + 130.0f);
                        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(0, 0, 0, 165), 4.0f);
                        drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 32), 4.0f);

                        char titleBuffer[96]{};
                        std::snprintf(titleBuffer, sizeof(titleBuffer), "FPS %d", static_cast<int>(std::round(fps)));
                        drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 6.0f), statusColor, titleBuffer);

                        char frameAvgBuffer[160]{};
                        std::snprintf(frameAvgBuffer,
                                      sizeof(frameAvgBuffer),
                                      "Frame %.2f ms | Avg %.2f ms",
                                      deltaTimeMs,
                                      avgFrameMs);
                        drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 24.0f), IM_COL32(215, 230, 255, 255), frameAvgBuffer);

                        char minMaxBuffer[160]{};
                        std::snprintf(minMaxBuffer,
                                      sizeof(minMaxBuffer),
                                      "Min %.2f ms | Max %.2f ms",
                                      minFrameMs,
                                      maxFrameMs);
                        drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 40.0f), IM_COL32(215, 230, 255, 255), minMaxBuffer);

                        const ImVec2 graphMin(panelMin.x + 8.0f, panelMin.y + 58.0f);
                        const ImVec2 graphMax(panelMax.x - 8.0f, panelMax.y - 8.0f);
                        drawList->AddRectFilled(graphMin, graphMax, IM_COL32(20, 24, 30, 220), 3.0f);
                        drawList->AddRect(graphMin, graphMax, IM_COL32(255, 255, 255, 20), 3.0f);

                        const float graphHeight = graphMax.y - graphMin.y;
                        const float graphWidth = graphMax.x - graphMin.x;
                        const float graphMaxMs = std::max(50.0f, maxFrameMs * 1.2f);
                        auto msToY = [&](float milliseconds) {
                            const float normalized = std::clamp(milliseconds / graphMaxMs, 0.0f, 1.0f);
                            return graphMax.y - normalized * graphHeight;
                        };

                        const float y60 = msToY(16.67f);
                        const float y30 = msToY(33.33f);
                        drawList->AddLine(ImVec2(graphMin.x, y60), ImVec2(graphMax.x, y60), IM_COL32(110, 255, 120, 70), 1.0f);
                        drawList->AddLine(ImVec2(graphMin.x, y30), ImVec2(graphMax.x, y30), IM_COL32(255, 220, 90, 70), 1.0f);

                        if (fpsHistory.SampleCount >= 2)
                        {
                            const size_t baseIndex = (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount) % fpsHistory.FrameTimesMs.size();
                            for (size_t pointIndex = 1; pointIndex < fpsHistory.SampleCount; ++pointIndex)
                            {
                                const size_t sampleIndexA = (baseIndex + pointIndex - 1) % fpsHistory.FrameTimesMs.size();
                                const size_t sampleIndexB = (baseIndex + pointIndex) % fpsHistory.FrameTimesMs.size();
                                const float sampleA = fpsHistory.FrameTimesMs[sampleIndexA];
                                const float sampleB = fpsHistory.FrameTimesMs[sampleIndexB];

                                const float xA = graphMin.x + (static_cast<float>(pointIndex - 1) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                                const float xB = graphMin.x + (static_cast<float>(pointIndex) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                                const float yA = msToY(sampleA);
                                const float yB = msToY(sampleB);

                                drawList->AddLine(ImVec2(xA, yA), ImVec2(xB, yB), IM_COL32(120, 200, 255, 220), 1.8f);
                            }

                            drawList->AddCircleFilled(ImVec2(graphMax.x, msToY(deltaTimeMs)), 2.5f, statusColor);
                        }
                    }
                }
            }

            ImGui::End();
        }
        else
        {
            focusSceneViewRequested = false;
        }
        ImGui::PopStyleVar();

        SceneRenderer::SetUiInputViewportRectPixels(0.0f, 0.0f, 0.0f, 0.0f, false);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (showGameView)
        {
            if (focusGameViewRequested)
            {
                ImGui::SetNextWindowFocus();
                focusGameViewRequested = false;
            }
            const bool gameWindowVisible = ImGui::Begin("Game View", &showGameView);

            gameViewFocused = ImGui::IsWindowFocused();
            gameViewHovered = ImGui::IsWindowHovered();
            const bool skipGameRender = !gameWindowVisible || ImGui::IsWindowCollapsed();
            const ImVec2 gameViewSize = ImGui::GetContentRegionAvail();
            const uint32_t gameWidth = sanitizeViewportDimension(gameViewSize.x);
            const uint32_t gameHeight = sanitizeViewportDimension(gameViewSize.y);

            if (!skipGameRender && gameWidth > 0 && gameHeight > 0)
            {
                ensureGameViewFramebuffer(gameWidth, gameHeight);
                gameViewWidthPixels = gameWidth;
                gameViewHeightPixels = gameHeight;

                if (gameViewCamera)
                    gameViewCamera->SetViewportSize(gameWidth, gameHeight);

                const bool isSceneLoading = scene && scene->GetLoadState() == Scene::LoadState::Loading;
                if (gameViewCamera && gameViewFramebuffer && !isSceneLoading)
                {
                    if (renderGameView)
                        renderGameView(*gameViewCamera, gameViewFramebuffer, gameWidth, gameHeight);
                    else if (scene)
                        SceneRenderer::RenderToViewport(*scene, *gameViewCamera, gameViewFramebuffer, gameWidth, gameHeight);
                }

                if (gameViewFramebuffer && gameViewFramebuffer->GetColorAttachment())
                {
                    ImGui::Image(
                        static_cast<ImTextureID>(GetTextureNativeHandle(gameViewFramebuffer->GetColorAttachment())),
                        ImVec2(static_cast<float>(gameWidth), static_cast<float>(gameHeight)),
                        ImVec2(0, 1),
                        ImVec2(1, 0));
                    const ImVec2 gameRectMin = ImGui::GetItemRectMin();
                    const ImVec2 gameRectMax = ImGui::GetItemRectMax();
                    gameViewRectValid = true;
                    gameViewRectMinPixels = glm::vec2(gameRectMin.x, gameRectMin.y);
                    gameViewRectMaxPixels = glm::vec2(gameRectMax.x, gameRectMax.y);

                    const ImVec2 minPos = gameRectMin;
                    const ImVec2 maxPos = gameRectMax;
                    SceneRenderer::SetUiInputViewportRectPixels(
                        minPos.x,
                        minPos.y,
                        maxPos.x - minPos.x,
                        maxPos.y - minPos.y,
                        true);
                    const bool gameLoadingToastDrawn = drawLoadingOverlay(minPos, maxPos);
                    if (!gameLoadingToastDrawn && showMissingGameplayCameraOverlay)
                    {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

                        const char* text = "Game View: No active gameplay camera.\nAdd a Camera Component to an entity and set it as Primary.";
                        const ImVec2 textSize = ImGui::CalcTextSize(text);
                        const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                        drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                    }
                }
            }

            ImGui::End();
        }
        else
        {
            focusGameViewRequested = false;
        }
        ImGui::PopStyleVar();
    }
}
