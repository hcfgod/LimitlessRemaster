#pragma once

#include "EditorPlayMode.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace Limitless
{
    class CameraManager;
    class EditorCameraController;
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
              const std::function<void(uint32_t, uint32_t)>& ensureViewportFramebuffer);
}
