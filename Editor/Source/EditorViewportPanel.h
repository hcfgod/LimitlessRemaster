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
    class CameraManager;
    class EditorCameraController;
    class EditorUndoService;
    class Framebuffer;
    class Scene;
}

namespace Limitless::EditorViewportPanel
{
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
              EditorUndoService* undoService,
              const char* materialPayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey);
}
