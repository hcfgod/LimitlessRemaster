#include "Graphics/Renderer2D.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"

#include "Graphics/Buffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/VertexArray.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

namespace Limitless
{
    namespace
    {
        struct QuadVertex
        {
            glm::vec3 Position{0.0f};
            glm::vec2 UV{0.0f};
            glm::vec4 Color{1.0f};
        };

        struct Renderer2DData
        {
            static constexpr uint32_t kMaxQuads = 10000;
            static constexpr uint32_t kMaxVertices = kMaxQuads * 4;
            static constexpr uint32_t kMaxIndices = kMaxQuads * 6;

            bool Initialized = false;

            std::shared_ptr<VertexArray> QuadVertexArray;
            std::shared_ptr<VertexBuffer> QuadVertexBuffer;
            std::shared_ptr<IndexBuffer> QuadIndexBuffer;

            Assets::MaterialAsset::Ptr Material;
            std::shared_ptr<Shader> Shader;

            std::shared_ptr<Texture2D> CurrentTexture;

            glm::mat4 ViewProjection{1.0f};

            std::vector<QuadVertex> VertexStaging;
            uint32_t IndexCount = 0;

            Renderer2D::Statistics Stats{};
        };

        Renderer2DData g_Data;

        static glm::mat4 MakeQuadTransform2D(const glm::vec2& position, const glm::vec2& size)
        {
            glm::mat4 transform(1.0f);
            transform = glm::translate(transform, glm::vec3(position, 0.0f));
            transform = glm::scale(transform, glm::vec3(size, 1.0f));
            return transform;
        }

        static void EnsureInitialized()
        {
            if (!g_Data.Initialized)
            {
                Renderer2D::Initialize();
            }
        }
    }

    void Renderer2D::Initialize()
    {
        if (g_Data.Initialized)
        {
            return;
        }

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
        {
            LT_CORE_WARN("Renderer2D::Initialize called before Renderer is initialized (skipping)");
            return;
        }

        g_Data.QuadVertexArray = VertexArray::Create();
        g_Data.QuadVertexBuffer = VertexBuffer::Create(Renderer2DData::kMaxVertices * static_cast<uint32_t>(sizeof(QuadVertex)));

        g_Data.QuadVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_UV" },
            { ShaderDataType::Float4, "a_Color" }
        });
        g_Data.QuadVertexArray->AddVertexBuffer(g_Data.QuadVertexBuffer);

        std::vector<uint32_t> indices;
        indices.resize(Renderer2DData::kMaxIndices);
        uint32_t offset = 0;
        for (uint32_t i = 0; i < Renderer2DData::kMaxIndices; i += 6)
        {
            indices[i + 0] = offset + 0;
            indices[i + 1] = offset + 1;
            indices[i + 2] = offset + 2;

            indices[i + 3] = offset + 2;
            indices[i + 4] = offset + 3;
            indices[i + 5] = offset + 0;

            offset += 4;
        }

        g_Data.QuadIndexBuffer = IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
        g_Data.QuadVertexArray->SetIndexBuffer(g_Data.QuadIndexBuffer);

        // Default 2D material (shader + placeholder texture ref).
        g_Data.Material = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>("Assets/Materials/Renderer2D_TexturedQuad.material.json");
        if (!g_Data.Material)
        {
            LT_CORE_ERROR("Renderer2D: failed to load default material asset (Renderer2D_TexturedQuad)");
            return;
        }

        g_Data.Shader = g_Data.Material->GetShader();
        if (!g_Data.Shader)
        {
            LT_CORE_ERROR("Renderer2D: default material shader is not ready yet");
            // Keep initialized; the shader can arrive later via the asset system.
        }
        else
        {
            // Be explicit: sampler is bound to texture unit 0.
            g_Data.Shader->SetInt("u_Texture", 0);
        }

        // Default texture (from material). This is used when DrawQuad is called without a texture.
        g_Data.CurrentTexture = g_Data.Material->GetMainTexture();

        g_Data.VertexStaging.reserve(Renderer2DData::kMaxVertices);
        g_Data.Initialized = true;

        LT_CORE_INFO("Renderer2D initialized (MaxQuadsPerBatch={})", Renderer2DData::kMaxQuads);
    }

    void Renderer2D::Shutdown()
    {
        g_Data = {};
    }

    void Renderer2D::BeginScene(const Camera& camera)
    {
        EnsureInitialized();
        if (!g_Data.Initialized)
        {
            return;
        }

        BeginScene(camera.GetViewProjectionMatrix());
    }

    void Renderer2D::BeginScene(const glm::mat4& viewProjection)
    {
        EnsureInitialized();
        if (!g_Data.Initialized)
        {
            return;
        }

        g_Data.ViewProjection = viewProjection;
        g_Data.IndexCount = 0;
        g_Data.VertexStaging.clear();

        // Typical 2D defaults: alpha blending on, depth/cull off.
        auto& renderer = Renderer::GetInstance();
        renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, true));
        renderer.SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
        renderer.SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
    }

    void Renderer2D::EndScene()
    {
        if (!g_Data.Initialized)
        {
            return;
        }

        Flush();
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawQuad(MakeQuadTransform2D(position, size), color);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor)
    {
        DrawQuad(MakeQuadTransform2D(position, size), texture, tintColor);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color)
    {
        EnsureInitialized();
        if (!g_Data.Initialized)
        {
            return;
        }

        // If we have no texture at all, there is nothing to sample. Keep it obvious in logs.
        if (!g_Data.CurrentTexture)
        {
            // Try to recover from material readiness changes.
            if (g_Data.Material)
            {
                g_Data.CurrentTexture = g_Data.Material->GetMainTexture();
            }
        }

        // Batch overflow guard.
        if (g_Data.IndexCount + 6 > Renderer2DData::kMaxIndices)
        {
            Flush();
        }

        constexpr std::array<glm::vec4, 4> kQuadPositions = {
            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
            glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f),
        };

        constexpr std::array<glm::vec2, 4> kUVs = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
        };

        for (size_t i = 0; i < 4; ++i)
        {
            QuadVertex v;
            v.Position = glm::vec3(transform * kQuadPositions[i]);
            v.UV = kUVs[i];
            v.Color = color;
            g_Data.VertexStaging.push_back(v);
        }

        g_Data.IndexCount += 6;
        g_Data.Stats.QuadCount += 1;
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor)
    {
        EnsureInitialized();
        if (!g_Data.Initialized)
        {
            return;
        }

        std::shared_ptr<Texture2D> tex = texture ? texture->GetTexture() : nullptr;
        if (!tex)
        {
            // If not ready, treat as "missing" and fall back to whatever current default is.
            DrawQuad(transform, tintColor);
            return;
        }

        // Texture batching: one draw call per texture per scene in this MVP.
        if (g_Data.CurrentTexture && g_Data.CurrentTexture != tex && g_Data.IndexCount != 0)
        {
            Flush();
        }

        g_Data.CurrentTexture = tex;
        DrawQuad(transform, tintColor);
    }

    void Renderer2D::Flush()
    {
        if (!g_Data.Initialized)
        {
            return;
        }

        if (g_Data.IndexCount == 0 || g_Data.VertexStaging.empty())
        {
            return;
        }

        // Re-fetch shader in case the asset became ready after initialization.
        if (g_Data.Material && !g_Data.Shader)
        {
            g_Data.Shader = g_Data.Material->GetShader();
            if (g_Data.Shader)
            {
                g_Data.Shader->SetInt("u_Texture", 0);
            }
        }

        if (!g_Data.Shader)
        {
            LT_CORE_WARN("Renderer2D::Flush: shader not ready (dropping {} quads)", g_Data.IndexCount / 6);
            g_Data.IndexCount = 0;
            g_Data.VertexStaging.clear();
            return;
        }

        auto& renderer = Renderer::GetInstance();

        // Upload CPU-staged vertices to the GPU buffer on the render thread.
        const uint32_t dataSizeBytes = static_cast<uint32_t>(g_Data.VertexStaging.size() * sizeof(QuadVertex));
        renderer.SubmitCommand(std::make_unique<SetVertexBufferDataCommand>(g_Data.QuadVertexBuffer, g_Data.VertexStaging.data(), dataSizeBytes));

        // Bind pipeline state and issue a single indexed draw.
        renderer.SubmitCommand(std::make_unique<BindShaderCommand>(g_Data.Shader));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(g_Data.Shader, "u_ViewProjection", g_Data.ViewProjection));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(g_Data.Shader, "u_Model", glm::mat4(1.0f)));
        renderer.SubmitCommand(std::make_unique<BindTextureCommand>(g_Data.CurrentTexture, 0));
        renderer.SubmitCommand(std::make_unique<BindVertexArrayCommand>(g_Data.QuadVertexArray));
        renderer.SubmitCommand(std::make_unique<DrawIndexedCommand>(DrawMode::Triangles, g_Data.IndexCount, IndexType::UnsignedInt, nullptr, 0));

        g_Data.Stats.DrawCalls += 1;
        g_Data.Stats.Batches += 1;

        // Reset batch.
        g_Data.IndexCount = 0;
        g_Data.VertexStaging.clear();
    }

    const Renderer2D::Statistics& Renderer2D::GetStatistics()
    {
        return g_Data.Stats;
    }

    void Renderer2D::ResetStatistics()
    {
        g_Data.Stats.Reset();
    }
}

