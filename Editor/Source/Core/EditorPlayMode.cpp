#include "EditorPlayMode.h"

#include "Core/Time.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Limitless
{
    namespace
    {
        struct EntitySelectionPath final
        {
            std::vector<size_t> ChildIndices;
            std::vector<std::string> Tags;
        };

        std::string GetEntityTag(const Scene& scene, entt::entity entity)
        {
            if (!scene.IsValid(entity))
                return {};

            const auto* tag = scene.GetRegistry().try_get<TagComponent>(entity);
            return tag ? tag->Tag : std::string{};
        }

        EntitySelectionPath CaptureSelectionPath(const Scene* scene, entt::entity selectedEntity)
        {
            EntitySelectionPath path{};
            if (!scene || !scene->IsValid(selectedEntity))
                return path;

            entt::entity current = selectedEntity;
            while (current != entt::null && scene->IsValid(current))
            {
                const entt::entity parent = scene->GetParent(current);
                const std::vector<entt::entity> siblings = scene->GetChildren(parent);
                const auto foundSibling = std::find(siblings.begin(), siblings.end(), current);
                if (foundSibling == siblings.end())
                    return {};

                path.ChildIndices.push_back(static_cast<size_t>(std::distance(siblings.begin(), foundSibling)));
                path.Tags.push_back(GetEntityTag(*scene, current));
                current = parent;
            }

            std::reverse(path.ChildIndices.begin(), path.ChildIndices.end());
            std::reverse(path.Tags.begin(), path.Tags.end());
            return path;
        }

        entt::entity ResolveSelectionPath(Scene* scene, const EntitySelectionPath& path)
        {
            if (!scene || path.ChildIndices.empty() || path.ChildIndices.size() != path.Tags.size())
                return entt::null;

            entt::entity parent = entt::null;
            entt::entity current = entt::null;
            for (size_t level = 0; level < path.ChildIndices.size(); ++level)
            {
                const std::vector<entt::entity> siblings = scene->GetChildren(parent);
                if (siblings.empty())
                    return entt::null;

                const size_t siblingIndex = path.ChildIndices[level];
                entt::entity candidate = siblingIndex < siblings.size() ? siblings[siblingIndex] : entt::null;
                const std::string& expectedTag = path.Tags[level];

                // When sibling order changed during runtime, use tag as a fallback heuristic.
                if (!expectedTag.empty() &&
                    (candidate == entt::null || GetEntityTag(*scene, candidate) != expectedTag))
                {
                    const auto tagMatch = std::find_if(
                        siblings.begin(),
                        siblings.end(),
                        [&](entt::entity sibling) { return GetEntityTag(*scene, sibling) == expectedTag; });
                    if (tagMatch != siblings.end())
                        candidate = *tagMatch;
                }

                if (candidate == entt::null || !scene->IsValid(candidate))
                    return entt::null;

                current = candidate;
                parent = candidate;
            }

            return current;
        }

        std::optional<CameraId> FindFirstGameplayCamera(CameraManager& cameraManager)
        {
            for (CameraId id : cameraManager.GetAllCameraIds())
            {
                if (const Camera* camera = cameraManager.GetCamera(id))
                {
                    if (camera->GetUsage() == CameraUsage::Gameplay)
                        return id;
                }
            }

            return std::nullopt;
        }

        struct GameplaySceneCameraSelection
        {
            entt::entity Entity = entt::null;
            CameraComponent Component{};
        };

        std::optional<GameplaySceneCameraSelection> FindGameplaySceneCamera(Scene& scene)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<CameraComponent>();
            std::optional<GameplaySceneCameraSelection> fallback{};

            for (entt::entity entity : view)
            {
                const auto& camera = view.get<CameraComponent>(entity);
                if (!fallback.has_value())
                    fallback = GameplaySceneCameraSelection{ entity, camera };

                if (camera.IsPrimary)
                    return GameplaySceneCameraSelection{ entity, camera };
            }

            return fallback;
        }

        void SetCameraTransformFromEntity(const Scene& scene,
                                          entt::entity entity,
                                          CameraManager& cameraManager,
                                          CameraId cameraId)
        {
            const glm::mat4 worldTransform = scene.GetWorldTransformMatrix(entity);
            const glm::vec3 position = glm::vec3(worldTransform[3]);

            if (auto* orthographicCamera = cameraManager.GetOrthographic2D(cameraId))
            {
                orthographicCamera->SetPosition(position);
                const float rotationRadians = std::atan2(worldTransform[1][0], worldTransform[0][0]);
                orthographicCamera->SetRotationRadians(rotationRadians);
            }
            else if (auto* perspectiveCamera = cameraManager.GetPerspective3D(cameraId))
            {
                perspectiveCamera->SetPosition(position);

                // Use transformed local -Z as forward so identity transform looks toward world -Z.
                const glm::vec3 forward = glm::normalize(glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                const float yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));
                const float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
                perspectiveCamera->SetYawPitchDegrees(yawDegrees, pitchDegrees);
            }
        }

        std::optional<CameraId> CreateGameplayCameraFromScene(Scene& scene,
                                                               CameraManager& cameraManager,
                                                               uint32_t viewportWidthPixels,
                                                               uint32_t viewportHeightPixels)
        {
            const auto selection = FindGameplaySceneCamera(scene);
            if (!selection.has_value())
                return std::nullopt;

            CameraId createdId{};
            const CameraComponent& component = selection->Component;
            if (component.Projection == CameraComponent::ProjectionType::Perspective3D)
            {
                CameraManager::Perspective3DCreateInfo createInfo{};
                createInfo.Name = "Scene Gameplay Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.FieldOfViewYDegrees = component.FieldOfViewYDegrees;

                // If this component came from orthographic defaults and user just switched projection,
                // promote clip planes to perspective-safe defaults to avoid clipping everything.
                if (component.NearPlane <= 0.0f && component.FarPlane <= 1.0f)
                {
                    createInfo.NearPlane = 0.1f;
                    createInfo.FarPlane = 1000.0f;
                }
                else
                {
                    createInfo.NearPlane = component.NearPlane > 0.0f ? component.NearPlane : 0.01f;
                    createInfo.FarPlane = component.FarPlane > createInfo.NearPlane ? component.FarPlane : createInfo.NearPlane + 1000.0f;
                }
                createdId = cameraManager.CreatePerspective3D(createInfo);
            }
            else
            {
                CameraManager::Orthographic2DCreateInfo createInfo{};
                createInfo.Name = "Scene Gameplay Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.Zoom = component.Zoom > 0.0f ? component.Zoom : 1.0f;
                createInfo.NearPlane = component.NearPlane;
                createInfo.FarPlane = component.FarPlane > component.NearPlane ? component.FarPlane : component.NearPlane + 2.0f;
                createdId = cameraManager.CreateOrthographic2D(createInfo);
            }

            SetCameraTransformFromEntity(scene, selection->Entity, cameraManager, createdId);
            return createdId;
        }

        std::optional<CameraId> CreateGameplayCameraFromSelection(Scene& scene,
                                                                   CameraManager& cameraManager,
                                                                   const GameplaySceneCameraSelection& selection,
                                                                   uint32_t viewportWidthPixels,
                                                                   uint32_t viewportHeightPixels)
        {
            CameraId createdId{};
            const CameraComponent& component = selection.Component;
            if (component.Projection == CameraComponent::ProjectionType::Perspective3D)
            {
                CameraManager::Perspective3DCreateInfo createInfo{};
                createInfo.Name = "Scene Gameplay Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.FieldOfViewYDegrees = component.FieldOfViewYDegrees;

                // If this component came from orthographic defaults and user just switched projection,
                // promote clip planes to perspective-safe defaults to avoid clipping everything.
                if (component.NearPlane <= 0.0f && component.FarPlane <= 1.0f)
                {
                    createInfo.NearPlane = 0.1f;
                    createInfo.FarPlane = 1000.0f;
                }
                else
                {
                    createInfo.NearPlane = component.NearPlane > 0.0f ? component.NearPlane : 0.01f;
                    createInfo.FarPlane = component.FarPlane > createInfo.NearPlane ? component.FarPlane : createInfo.NearPlane + 1000.0f;
                }
                createdId = cameraManager.CreatePerspective3D(createInfo);
            }
            else
            {
                CameraManager::Orthographic2DCreateInfo createInfo{};
                createInfo.Name = "Scene Gameplay Camera";
                createInfo.Usage = CameraUsage::Gameplay;
                createInfo.ViewportWidthPixels = viewportWidthPixels;
                createInfo.ViewportHeightPixels = viewportHeightPixels;
                createInfo.Zoom = component.Zoom > 0.0f ? component.Zoom : 1.0f;
                createInfo.NearPlane = component.NearPlane;
                createInfo.FarPlane = component.FarPlane > component.NearPlane ? component.FarPlane : component.NearPlane + 2.0f;
                createdId = cameraManager.CreateOrthographic2D(createInfo);
            }

            SetCameraTransformFromEntity(scene, selection.Entity, cameraManager, createdId);
            return createdId;
        }
    }

    namespace EditorPlayMode
    {
        void Enter(EditorPlayModeState& playModeState,
                   SceneCollectionSlot& scene,
                   SceneCollectionSlot& editSceneStored,
                   CameraManager& cameraManager,
                   CameraId editorCameraId,
                   uint32_t viewportWidthPixels,
                   uint32_t viewportHeightPixels,
                   CameraId& cachedGameplayCameraId,
                   bool& createdGameplayCameraFromScene,
                   bool& playModeMissingGameplayCamera,
                   entt::entity& selectedEntity,
                   std::string& selectedTextureAssetKey,
                   Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (playModeState != EditorPlayModeState::Edit)
                return;

            const EntitySelectionPath selectedEntityPath = CaptureSelectionPath(scene.get(), selectedEntity);
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();

            const std::string sceneAssetKey = scene.GetAssetKey();
            std::unique_ptr<Scene> editScene = scene.Release();
            editSceneStored.SetOwnedScene(
                std::move(editScene),
                sceneAssetKey,
                SceneCollectionLifecycleState::Suspended,
                SceneRole::EditAuthoring | SceneRole::Render);
            scene.SetOwnedScene(
                editSceneStored ? editSceneStored->Clone() : std::make_unique<Scene>(),
                sceneAssetKey,
                SceneCollectionLifecycleState::Active,
                SceneRole::GameplayPrimary |
                    SceneRole::RuntimeUpdate |
                    SceneRole::FixedUpdate |
                    SceneRole::Render |
                    SceneRole::ScriptQueryTarget |
                    SceneRole::AudioPlayback);
            selectedEntity = ResolveSelectionPath(scene.get(), selectedEntityPath);

            playModeMissingGameplayCamera = false;
            createdGameplayCameraFromScene = false;
            const std::optional<CameraId> gameplayCameraId = scene
                ? CreateGameplayCameraFromScene(*scene, cameraManager, viewportWidthPixels, viewportHeightPixels)
                : std::nullopt;
            if (gameplayCameraId.has_value())
            {
                createdGameplayCameraFromScene = true;
                cachedGameplayCameraId = gameplayCameraId.value();
                cameraManager.SetActiveCamera(cachedGameplayCameraId);
            }
            else
            {
                const std::optional<CameraId> existingGameplayCameraId = FindFirstGameplayCamera(cameraManager);
                if (existingGameplayCameraId.has_value())
                {
                    cachedGameplayCameraId = existingGameplayCameraId.value();
                    cameraManager.SetActiveCamera(cachedGameplayCameraId);
                }
                else
                {
                    playModeMissingGameplayCamera = true;
                    cachedGameplayCameraId = {};
                    cameraManager.SetActiveCamera(editorCameraId);
                }
            }

            Limitless::Time::SetTimeScale(1.0f);
            playModeState = EditorPlayModeState::Play;
        }

        void EnterSimulate(EditorPlayModeState& playModeState,
                           SceneCollectionSlot& scene,
                           SceneCollectionSlot& editSceneStored,
                           CameraManager& cameraManager,
                           CameraId editorCameraId,
                           uint32_t viewportWidthPixels,
                           uint32_t viewportHeightPixels,
                           CameraId& cachedGameplayCameraId,
                           bool& createdGameplayCameraFromScene,
                           bool& playModeMissingGameplayCamera,
                           entt::entity& selectedEntity,
                           std::string& selectedTextureAssetKey,
                           Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            Enter(
                playModeState,
                scene,
                editSceneStored,
                cameraManager,
                editorCameraId,
                viewportWidthPixels,
                viewportHeightPixels,
                cachedGameplayCameraId,
                createdGameplayCameraFromScene,
                playModeMissingGameplayCamera,
                selectedEntity,
                selectedTextureAssetKey,
                cachedTextureAsset);
            if (playModeState == EditorPlayModeState::Play)
                playModeState = EditorPlayModeState::Simulate;
        }

        void Exit(EditorPlayModeState& playModeState,
                  SceneCollectionSlot& scene,
                  SceneCollectionSlot& editSceneStored,
                  CameraManager& cameraManager,
                  CameraId editorCameraId,
                  CameraId& cachedGameplayCameraId,
                  bool& createdGameplayCameraFromScene,
                  bool& playModeMissingGameplayCamera,
                  entt::entity& selectedEntity,
                  std::string& selectedTextureAssetKey,
                  Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (playModeState == EditorPlayModeState::Edit)
                return;

            const EntitySelectionPath selectedEntityPath = CaptureSelectionPath(scene.get(), selectedEntity);
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();

            if (editSceneStored)
            {
                const std::string sceneAssetKey = editSceneStored.GetAssetKey();
                std::unique_ptr<Scene> restoredEditScene = editSceneStored.Release();
                scene.SetOwnedScene(
                    std::move(restoredEditScene),
                    sceneAssetKey,
                    SceneCollectionLifecycleState::Active,
                    SceneRole::EditAuthoring | SceneRole::Render);
            }

            selectedEntity = ResolveSelectionPath(scene.get(), selectedEntityPath);

            if (createdGameplayCameraFromScene && cachedGameplayCameraId)
                cameraManager.DestroyCamera(cachedGameplayCameraId);

            playModeMissingGameplayCamera = false;
            createdGameplayCameraFromScene = false;
            cachedGameplayCameraId = {};
            cameraManager.SetActiveCamera(editorCameraId);
            Limitless::Time::SetTimeScale(1.0f);
            playModeState = EditorPlayModeState::Edit;
        }

        void SyncSceneCamera(EditorPlayModeState playModeState,
                             Scene* scene,
                             CameraManager& cameraManager,
                             CameraId& cachedGameplayCameraId,
                             bool createdGameplayCameraFromScene)
        {
            if (!createdGameplayCameraFromScene ||
                (playModeState != EditorPlayModeState::Play &&
                 playModeState != EditorPlayModeState::Simulate &&
                 playModeState != EditorPlayModeState::Pause) ||
                !scene ||
                !cachedGameplayCameraId)
            {
                return;
            }

            const auto selection = FindGameplaySceneCamera(*scene);
            if (!selection.has_value())
                return;

            const CameraComponent& component = selection->Component;
            const Camera* existingCamera = cameraManager.GetCamera(cachedGameplayCameraId);
            if (!existingCamera)
                return;

            const CameraType expectedType = (component.Projection == CameraComponent::ProjectionType::Perspective3D)
                ? CameraType::Perspective3D
                : CameraType::Orthographic2D;

            // Projection type changed during play mode: rebuild the temporary scene gameplay camera
            // so the runtime camera class matches the component projection.
            if (existingCamera->GetType() != expectedType)
            {
                cameraManager.DestroyCamera(cachedGameplayCameraId);
                const auto recreatedCameraId = CreateGameplayCameraFromSelection(*scene, cameraManager, *selection, 1, 1);
                if (!recreatedCameraId.has_value())
                    return;

                cachedGameplayCameraId = recreatedCameraId.value();
                cameraManager.SetActiveCamera(cachedGameplayCameraId);
            }

            if (component.Projection == CameraComponent::ProjectionType::Orthographic2D)
            {
                auto* orthographicCamera = cameraManager.GetOrthographic2D(cachedGameplayCameraId);
                if (!orthographicCamera)
                    return;

                const float zoom = component.Zoom > 0.0f ? component.Zoom : 1.0f;
                const float nearPlane = component.NearPlane;
                const float farPlane = component.FarPlane > nearPlane ? component.FarPlane : nearPlane + 2.0f;
                orthographicCamera->SetProjection(zoom, nearPlane, farPlane);
            }
            else
            {
                auto* perspectiveCamera = cameraManager.GetPerspective3D(cachedGameplayCameraId);
                if (!perspectiveCamera)
                    return;

                const float fieldOfViewY = component.FieldOfViewYDegrees > 1.0f ? component.FieldOfViewYDegrees : 60.0f;
                float nearPlane = component.NearPlane > 0.0f ? component.NearPlane : 0.01f;
                float farPlane = component.FarPlane > nearPlane ? component.FarPlane : nearPlane + 1000.0f;
                if (component.NearPlane <= 0.0f && component.FarPlane <= 1.0f)
                {
                    nearPlane = 0.1f;
                    farPlane = 1000.0f;
                }
                perspectiveCamera->SetPerspective(fieldOfViewY, nearPlane, farPlane);
            }

            SetCameraTransformFromEntity(*scene, selection->Entity, cameraManager, cachedGameplayCameraId);
        }

        void TogglePause(EditorPlayModeState& playModeState)
        {
            if (playModeState == EditorPlayModeState::Edit)
                return;

            if (playModeState == EditorPlayModeState::Pause)
            {
                Limitless::Time::SetTimeScale(1.0f);
                playModeState = EditorPlayModeState::Play;
            }
            else
            {
                Limitless::Time::SetTimeScale(0.0f);
                playModeState = EditorPlayModeState::Pause;
            }
        }
    }
}
