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
    //
    // Instancing:
    // - Renderer2D is fully instantiable. Multiple instances can coexist for
    //   split-screen, multi-viewport editors, or isolated test contexts.
    // - Renderer2D::Default() returns the global default instance for convenience.
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

        Renderer2D();
        ~Renderer2D();

        Renderer2D(const Renderer2D&) = delete;
        Renderer2D& operator=(const Renderer2D&) = delete;

        void Initialize();
        void Shutdown();

        void BeginScene(const Camera& camera);
        void BeginScene(const glm::mat4& viewProjection);
        void BeginScene(const glm::mat4& viewProjection, bool enableDepthTest);
        void EndScene();

        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor = glm::vec4(1.0f));

        void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
        void DrawQuad(const glm::mat4& transform, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor = glm::vec4(1.0f));
        void DrawQuad(const glm::mat4& transform,
                     const Assets::TextureAsset::Ptr& texture,
                     const glm::vec4& tintColor,
                     const glm::vec2& uvMin,
                     const glm::vec2& uvMax);
        void DrawText(const glm::mat4& transform, const std::string& text, const Font::Ptr& font, float fontSize, const glm::vec4& color = glm::vec4(1.0f));

        /// Submit pre-baked quad vertices directly, bypassing per-quad transform
        /// computation. Used by tilemap chunk rendering to batch an entire chunk
        /// as a single draw call. Vertices must use the same layout as Renderer2D
        /// internal QuadVertex (Position vec3, UV vec2, Color vec4, TexIndex int).
        /// Texture slots are bound in order; vertex TexIndex values must index
        /// into the provided texture array.
        void SubmitPrebakedQuads(const void* vertexData,
                                 uint32_t quadCount,
                                 const std::shared_ptr<Texture2D>* textures,
                                 uint32_t textureCount,
                                 const glm::mat4& modelTransform = glm::mat4(1.0f));

        const Statistics& GetStatistics() const;
        void ResetStatistics();

        bool IsShaderReady();

        static const char* GetDefaultShaderKey();

        /// Returns the global default instance. Asserts if none has been initialized.
        static Renderer2D& Default();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        void EnsureInitialized();
        void FlushQuadBatch();
        void FlushTextBatch();

        static Renderer2D* s_Default;
    };
}
