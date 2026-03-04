#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace Limitless
{
    class Camera;
    class Framebuffer;
    class Scene;

    // -----------------------------------------------------------------------------
    // SceneRenderer
    // Renders scene entities to the given camera. Uses Renderer2D for SpriteComponent.
    // -----------------------------------------------------------------------------
    class SceneRenderer
    {
    public:
        /// Render scene to the given camera (draws to whatever framebuffer is currently bound).
        static void Render(Scene& scene, const Camera& camera);

        /// Sets the viewport background clear color used before scene rendering.
        static void SetViewportClearColor(const glm::vec4& clearColor);
        /// Gets the currently configured viewport background clear color.
        static glm::vec4 GetViewportClearColor();
        /// Sets the UI input viewport rectangle in window-space pixels.
        /// When enabled, Canvas UI pointer hit-testing uses this rectangle instead of the full window.
        static void SetUiInputViewportRectPixels(float minX, float minY, float width, float height, bool enabled = true);

        /// Render scene to a viewport framebuffer (binds, clears, draws, unbinds).
        /// Use this for editor viewports or off-screen rendering.
        static void RenderToViewport(Scene& scene, const Camera& camera,
            const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height);
    };
}
