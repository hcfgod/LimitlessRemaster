#pragma once

#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Font.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // Renderer2D
    // A small but "real" 2D renderer API surface intended for game code.
    //
    // Goals:
    // - BeginScene/DrawQuad/EndScene API (stable gameplay seam)
    // - Dynamic quad batching (one draw call per texture per scene in the current MVP)
    // - Simple, visible statistics for profiling (draw calls / batches / quad count)
    //
    // Notes:
    // - This renderer intentionally stays OpenGL-first for now, built on the existing
    //   RenderCommandQueue infrastructure.
    // - Transforms are baked into vertices so we can batch without per-draw uniforms.
    // -----------------------------------------------------------------------------
    class Renderer2D final
    {
    public:
        struct Statistics
        {
            uint32_t DrawCalls = 0;
            uint32_t Batches = 0;
            uint32_t QuadCount = 0;

            void Reset()
            {
                DrawCalls = 0;
                Batches = 0;
                QuadCount = 0;
            }
        };

        Renderer2D() = delete;

        static void Initialize();
        static void Shutdown();

        // Begin a 2D rendering scene using any camera type (orthographic or perspective).
        // Renderer2D only requires a stable ViewProjection matrix.
        static void BeginScene(const Camera& camera);
        static void BeginScene(const glm::mat4& viewProjection);
        static void BeginScene(const glm::mat4& viewProjection, bool enableDepthTest);
        static void EndScene();

        // Position/size overloads (Z is 0 in this MVP).
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor = glm::vec4(1.0f));

        // Full transform overloads.
        static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
        static void DrawQuad(const glm::mat4& transform, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor = glm::vec4(1.0f));
        static void DrawText(const glm::mat4& transform, const std::string& text, const Font::Ptr& font, float fontSize, const glm::vec4& color = glm::vec4(1.0f));

        static const Statistics& GetStatistics();
        static void ResetStatistics();

        /// Returns true when the default material shader is loaded and ready for rendering.
        /// While false, DrawQuad calls are dropped silently and the caller may show a loading UI.
        static bool IsShaderReady();

        /// Asset key for the default Renderer2D shader. Used to query AssetLoadProgress.
        static const char* GetDefaultShaderKey();

    private:
        static void FlushQuadBatch();
        static void FlushTextBatch();
    };
}

