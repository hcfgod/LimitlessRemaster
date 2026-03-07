#pragma once

#include "Assets/TextureAsset.h"
#include "Limitless.h"
#include "Scene/SceneCollection.h"

#include <memory>
#include <string>

namespace Limitless
{
    class Scene;

    enum class EditorPlayModeState
    {
        Edit = 0,
        Play = 1,
        Simulate = 2,
        Pause = 3
    };

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
                   Assets::TextureAsset::Ptr& cachedTextureAsset);

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
                           Assets::TextureAsset::Ptr& cachedTextureAsset);

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
                  Assets::TextureAsset::Ptr& cachedTextureAsset);

        /// While in Play/Pause, syncs transform/settings from scene CameraComponent to the
        /// active temporary gameplay camera created from the scene.
        void SyncSceneCamera(EditorPlayModeState playModeState,
                             Scene* scene,
                             CameraManager& cameraManager,
                             CameraId& cachedGameplayCameraId,
                             bool createdGameplayCameraFromScene);

        void TogglePause(EditorPlayModeState& playModeState);
    }
}
