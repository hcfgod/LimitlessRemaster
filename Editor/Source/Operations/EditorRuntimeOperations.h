#pragma once

#include "EditorPlayMode.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace Limitless
{
    class CameraManager;
    class EditorCameraController;
    class Framebuffer;
    class Scene;
}

namespace Limitless::EditorRuntimeOperations
{
    /// Initializes editor runtime systems and an empty working scene container.
    void Attach(uint32_t viewportWidthPixels,
                uint32_t viewportHeightPixels,
                std::unique_ptr<Scene>& scene,
                CameraManager& cameraManager,
                CameraId& editorCameraId,
                std::unique_ptr<EditorCameraController>& editorCameraController,
                std::shared_ptr<Framebuffer>& viewportFramebuffer);

    /// Shuts down editor runtime systems and clears runtime-owned resources.
    void Detach(std::unique_ptr<Scene>& scene,
                std::unique_ptr<Scene>& editSceneStored,
                std::unique_ptr<EditorCameraController>& editorCameraController,
                std::shared_ptr<Framebuffer>& viewportFramebuffer);

    /// Updates editor runtime per-frame input and camera behavior.
    void Update(EditorPlayModeState playModeState,
                bool sceneViewHovered,
                bool sceneViewRectValid,
                const glm::vec2& sceneViewRectMinPixels,
                const glm::vec2& sceneViewRectMaxPixels,
                bool gameViewRectValid,
                const glm::vec2& gameViewRectMinPixels,
                const glm::vec2& gameViewRectMaxPixels,
                bool textInputWanted,
                float deltaTime,
                EditorCameraController* editorCameraController);

    /// Handles top-level window resize events and updates viewport targets.
    void HandleWindowResize(uint32_t width,
                            uint32_t height,
                            uint32_t& viewportWidthPixels,
                            uint32_t& viewportHeightPixels,
                            std::shared_ptr<Framebuffer>& viewportFramebuffer,
                            EditorCameraController* editorCameraController);

    /// Creates or resizes the viewport framebuffer and keeps camera aspect in sync.
    void EnsureViewportFramebuffer(uint32_t width,
                                   uint32_t height,
                                   std::shared_ptr<Framebuffer>& viewportFramebuffer,
                                   uint32_t& viewportWidthPixels,
                                   uint32_t& viewportHeightPixels,
                                   EditorCameraController* editorCameraController);
}
