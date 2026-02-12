#include "EditorPlayMode.h"

#include "Core/Time.h"
#include "Scene/Scene.h"

#include <optional>

namespace Limitless
{
    namespace
    {
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
    }

    namespace EditorPlayMode
    {
        void Enter(EditorPlayModeState& playModeState,
                   std::unique_ptr<Scene>& scene,
                   std::unique_ptr<Scene>& editSceneStored,
                   CameraManager& cameraManager,
                   CameraId editorCameraId,
                   CameraId& cachedGameplayCameraId,
                   bool& playModeMissingGameplayCamera,
                   entt::entity& selectedEntity,
                   std::string& selectedTextureAssetKey,
                   Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (playModeState != EditorPlayModeState::Edit)
                return;

            selectedEntity = entt::null;
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();

            editSceneStored = std::move(scene);
            scene = editSceneStored ? editSceneStored->Clone() : std::make_unique<Scene>();

            playModeMissingGameplayCamera = false;
            const std::optional<CameraId> gameplayCameraId = FindFirstGameplayCamera(cameraManager);
            if (gameplayCameraId.has_value())
            {
                cachedGameplayCameraId = gameplayCameraId.value();
                cameraManager.SetActiveCamera(cachedGameplayCameraId);
            }
            else
            {
                playModeMissingGameplayCamera = true;
                cachedGameplayCameraId = {};
                cameraManager.SetActiveCamera(editorCameraId);
            }

            Limitless::Time::SetTimeScale(1.0f);
            playModeState = EditorPlayModeState::Play;
        }

        void Exit(EditorPlayModeState& playModeState,
                  std::unique_ptr<Scene>& scene,
                  std::unique_ptr<Scene>& editSceneStored,
                  CameraManager& cameraManager,
                  CameraId editorCameraId,
                  CameraId& cachedGameplayCameraId,
                  bool& playModeMissingGameplayCamera,
                  entt::entity& selectedEntity,
                  std::string& selectedTextureAssetKey,
                  Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (playModeState == EditorPlayModeState::Edit)
                return;

            selectedEntity = entt::null;
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();

            if (editSceneStored)
                scene = std::move(editSceneStored);

            playModeMissingGameplayCamera = false;
            cachedGameplayCameraId = {};
            cameraManager.SetActiveCamera(editorCameraId);
            Limitless::Time::SetTimeScale(1.0f);
            playModeState = EditorPlayModeState::Edit;
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
