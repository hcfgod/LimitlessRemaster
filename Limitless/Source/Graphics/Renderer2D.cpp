#include "Graphics/Renderer2D.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"

#include "Graphics/Buffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/VertexArray.h"

#include <utf8.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
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
            int32_t TexIndex = 0;
        };

        struct Renderer2DData
        {
            static constexpr uint32_t kMaxQuads = 10000;
            static constexpr uint32_t kMaxVertices = kMaxQuads * 4;
            static constexpr uint32_t kMaxIndices = kMaxQuads * 6;
            static constexpr uint32_t kMaxTextureSlots = 16;

            bool Initialized = false;

            std::shared_ptr<VertexArray> QuadVertexArray;
            std::shared_ptr<VertexBuffer> QuadVertexBuffer;
            std::shared_ptr<IndexBuffer> QuadIndexBuffer;

            Assets::MaterialAsset::Ptr Material;
            std::shared_ptr<Shader> ShaderProgram;
            Assets::MaterialAsset::Ptr TextMaterial;
            std::shared_ptr<Shader> TextShaderProgram;

            std::shared_ptr<Texture2D> WhiteTexture;
            std::array<std::shared_ptr<Texture2D>, kMaxTextureSlots> TextureSlots{};
            uint32_t TextureSlotCount = 0;

            glm::mat4 ViewProjection{1.0f};

            std::unique_ptr<QuadVertex[]> VertexBufferBase;
            QuadVertex* VertexBufferPtr = nullptr;
            uint32_t IndexCount = 0;

            std::array<std::shared_ptr<Texture2D>, kMaxTextureSlots> TextTextureSlots{};
            uint32_t TextTextureSlotCount = 0;
            std::unique_ptr<QuadVertex[]> TextVertexBufferBase;
            QuadVertex* TextVertexBufferPtr = nullptr;
            uint32_t TextIndexCount = 0;

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
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Int,    "a_TexIndex" }
        });
        g_Data.QuadVertexArray->AddVertexBuffer(g_Data.QuadVertexBuffer);

        std::vector<uint16_t> indices;
        indices.resize(Renderer2DData::kMaxIndices);
        uint16_t offset = 0;
        for (uint32_t i = 0; i < Renderer2DData::kMaxIndices; i += 6)
        {
            indices[i + 0] = static_cast<uint16_t>(offset + 0);
            indices[i + 1] = static_cast<uint16_t>(offset + 1);
            indices[i + 2] = static_cast<uint16_t>(offset + 2);

            indices[i + 3] = static_cast<uint16_t>(offset + 2);
            indices[i + 4] = static_cast<uint16_t>(offset + 3);
            indices[i + 5] = static_cast<uint16_t>(offset + 0);

            offset = static_cast<uint16_t>(offset + 4);
        }

        g_Data.QuadIndexBuffer = IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
        g_Data.QuadVertexArray->SetIndexBuffer(g_Data.QuadIndexBuffer);

        // 1x1 white texture for untextured (color-only) quads and as a guaranteed slot 0.
        // This keeps the shader sampling path uniform and makes batching rules deterministic.
        const uint32_t whitePixelRGBA8 = 0xFFFFFFFFu;
        TextureSpecification whiteSpec{};
        whiteSpec.GenerateMipmaps = false;
        whiteSpec.MinFilter = TextureFilter::Nearest;
        whiteSpec.MagFilter = TextureFilter::Nearest;
        whiteSpec.WrapU = TextureWrap::ClampToEdge;
        whiteSpec.WrapV = TextureWrap::ClampToEdge;
        g_Data.WhiteTexture = Texture2D::CreateFromRGBA8(1, 1, &whitePixelRGBA8, whiteSpec);

        // Default 2D material (shader + placeholder texture ref).
        g_Data.Material = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>("Assets/Materials/Renderer2D_TexturedQuad.material.json");
        if (!g_Data.Material)
        {
            LT_CORE_ERROR("Renderer2D: failed to load default material asset (Renderer2D_TexturedQuad)");
            return;
        }

        g_Data.ShaderProgram = g_Data.Material->GetShader();
        if (!g_Data.ShaderProgram)
        {
            LT_CORE_INFO("Renderer2D: default material shader loading (view on-screen progress in viewport)");
            // Keep initialized; the shader can arrive later via the asset system.
        }
        else
        {
            // Bind sampler array once: u_Textures[i] -> texture unit i.
            std::array<int, Renderer2DData::kMaxTextureSlots> samplers{};
            for (uint32_t i = 0; i < Renderer2DData::kMaxTextureSlots; ++i)
            {
                samplers[i] = static_cast<int>(i);
            }
            g_Data.ShaderProgram->SetIntArray("u_Textures", samplers.data(), static_cast<uint32_t>(samplers.size()));
        }

        g_Data.TextMaterial = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>("Assets/Materials/Renderer2D_MSDFText.material.json");
        if (!g_Data.TextMaterial)
        {
            LT_CORE_ERROR("Renderer2D: failed to load text material asset (Renderer2D_MSDFText)");
            return;
        }

        g_Data.TextShaderProgram = g_Data.TextMaterial->GetShader();
        if (g_Data.TextShaderProgram)
        {
            std::array<int, Renderer2DData::kMaxTextureSlots> samplers{};
            for (uint32_t i = 0; i < Renderer2DData::kMaxTextureSlots; ++i)
            {
                samplers[i] = static_cast<int>(i);
            }
            g_Data.TextShaderProgram->SetIntArray("u_Textures", samplers.data(), static_cast<uint32_t>(samplers.size()));
        }
        else
        {
            LT_CORE_INFO("Renderer2D: text shader loading (MSDF text rendering will activate when ready)");
        }

        g_Data.VertexBufferBase = std::make_unique<QuadVertex[]>(Renderer2DData::kMaxVertices);
        g_Data.VertexBufferPtr = g_Data.VertexBufferBase.get();
        g_Data.TextVertexBufferBase = std::make_unique<QuadVertex[]>(Renderer2DData::kMaxVertices);
        g_Data.TextVertexBufferPtr = g_Data.TextVertexBufferBase.get();
        g_Data.Initialized = true;

        LT_CORE_INFO("Renderer2D initialized (MaxQuadsPerBatch={}, MaxTextureSlotsPerBatch={})",
                     Renderer2DData::kMaxQuads, Renderer2DData::kMaxTextureSlots);
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
        BeginScene(viewProjection, true);
    }

    void Renderer2D::BeginScene(const glm::mat4& viewProjection, bool enableDepthTest)
    {
        EnsureInitialized();
        if (!g_Data.Initialized)
        {
            return;
        }

        g_Data.ViewProjection = viewProjection;
        g_Data.IndexCount = 0;
        g_Data.VertexBufferPtr = g_Data.VertexBufferBase.get();
        g_Data.TextIndexCount = 0;
        g_Data.TextVertexBufferPtr = g_Data.TextVertexBufferBase.get();

        // Reset texture slots for the new scene.
        g_Data.TextureSlotCount = 1;
        g_Data.TextureSlots[0] = g_Data.WhiteTexture;
        g_Data.TextTextureSlotCount = 1;
        g_Data.TextTextureSlots[0] = g_Data.WhiteTexture;

        // Typical world-space 2D defaults: alpha blending on, cull off.
        // Keep depth testing enabled so mixed sprite/text content respects Z ordering.
        auto& renderer = Renderer::GetInstance();
        renderer.SubmitCommandArena<SetBlendModeCommand>(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, true);
        renderer.SubmitCommandArena<SetDepthTestCommand>(enableDepthTest);
        renderer.SubmitCommandArena<SetCullFaceCommand>(false);
    }

    void Renderer2D::EndScene()
    {
        if (!g_Data.Initialized)
        {
            return;
        }

        FlushQuadBatch();
        FlushTextBatch();
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

        // Batch overflow guard.
        if (g_Data.IndexCount + 6 > Renderer2DData::kMaxIndices)
        {
            FlushQuadBatch();
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

        // Slot 0 is always the white texture for color-only quads.
        constexpr int32_t texIndex = 0;

        QuadVertex* v = g_Data.VertexBufferPtr;
        for (size_t i = 0; i < 4; ++i)
        {
            v->Position = glm::vec3(transform * kQuadPositions[i]);
            v->UV = kUVs[i];
            v->Color = color;
            v->TexIndex = texIndex;
            ++v;
        }
        g_Data.VertexBufferPtr = v;

        g_Data.IndexCount += 6;
        g_Data.Stats.QuadCount += 1;
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Assets::TextureAsset::Ptr& texture, const glm::vec4& tintColor)
    {
        DrawQuad(transform, texture, tintColor, glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f));
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform,
                              const Assets::TextureAsset::Ptr& texture,
                              const glm::vec4& tintColor,
                              const glm::vec2& uvMin,
                              const glm::vec2& uvMax)
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

        // Batch overflow guard: ensure there is room for this quad before we compute texture slot indices.
        if (g_Data.IndexCount + 6 > Renderer2DData::kMaxIndices)
        {
            FlushQuadBatch();
        }

        // Find or allocate a texture slot for this batch.
        int32_t texIndex = -1;
        for (uint32_t i = 0; i < g_Data.TextureSlotCount; ++i)
        {
            if (g_Data.TextureSlots[i] == tex)
            {
                texIndex = static_cast<int32_t>(i);
                break;
            }
        }

        if (texIndex < 0)
        {
            // Not found in current batch.
            if (g_Data.TextureSlotCount >= Renderer2DData::kMaxTextureSlots)
            {
                // Texture slot overflow -> flush and start a new batch.
                FlushQuadBatch();
            }

            // After a flush, a new batch starts with only the white texture in slot 0.
            // Allocate the next slot for this texture.
            texIndex = static_cast<int32_t>(g_Data.TextureSlotCount);
            g_Data.TextureSlots[g_Data.TextureSlotCount] = tex;
            g_Data.TextureSlotCount++;
        }

        constexpr std::array<glm::vec4, 4> kQuadPositions = {
            glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
            glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
            glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f),
        };

        const std::array<glm::vec2, 4> uvs = {
            glm::vec2(uvMin.x, uvMin.y),
            glm::vec2(uvMax.x, uvMin.y),
            glm::vec2(uvMax.x, uvMax.y),
            glm::vec2(uvMin.x, uvMax.y),
        };

        QuadVertex* v = g_Data.VertexBufferPtr;
        for (size_t i = 0; i < 4; ++i)
        {
            v->Position = glm::vec3(transform * kQuadPositions[i]);
            v->UV = uvs[i];
            v->Color = tintColor;
            v->TexIndex = texIndex;
            ++v;
        }
        g_Data.VertexBufferPtr = v;

        g_Data.IndexCount += 6;
        g_Data.Stats.QuadCount += 1;
    }

    void Renderer2D::DrawText(const glm::mat4& transform, const std::string& text, const Font::Ptr& font, float fontSize, const glm::vec4& color)
    {
        EnsureInitialized();
        if (!g_Data.Initialized || !font || text.empty())
        {
            return;
        }

        std::shared_ptr<Texture2D> atlasTexture = font->GetAtlasTexture();
        if (!atlasTexture)
        {
            return;
        }

        int32_t texIndex = -1;
        for (uint32_t i = 0; i < g_Data.TextTextureSlotCount; ++i)
        {
            if (g_Data.TextTextureSlots[i] == atlasTexture)
            {
                texIndex = static_cast<int32_t>(i);
                break;
            }
        }

        if (texIndex < 0)
        {
            if (g_Data.TextTextureSlotCount >= Renderer2DData::kMaxTextureSlots)
            {
                FlushTextBatch();
            }
            texIndex = static_cast<int32_t>(g_Data.TextTextureSlotCount);
            g_Data.TextTextureSlots[g_Data.TextTextureSlotCount] = atlasTexture;
            ++g_Data.TextTextureSlotCount;
        }

        std::vector<msdf_atlas::unicode_t> codepoints;
        codepoints.reserve(text.size());
        msdf_atlas::utf8Decode(codepoints, text.c_str());
        if (codepoints.empty())
        {
            return;
        }

        const float safeFontSize = std::max(1.0f, fontSize);
        const float atlasScale = safeFontSize / std::max(1.0f, font->GetEmSize());
        const float lineAdvance = font->GetLineHeight() * atlasScale;

        float penX = 0.0f;
        float penY = 0.0f;

        for (size_t index = 0; index < codepoints.size(); ++index)
        {
            const uint32_t codepoint = static_cast<uint32_t>(codepoints[index]);
            if (codepoint == '\r')
            {
                continue;
            }
            if (codepoint == '\n')
            {
                penX = 0.0f;
                penY -= lineAdvance;
                continue;
            }

            const Font::Glyph* glyph = font->GetGlyph(codepoint);
            if (!glyph)
            {
                continue;
            }

            const uint32_t nextCodepoint = (index + 1 < codepoints.size()) ? static_cast<uint32_t>(codepoints[index + 1]) : 0u;
            const float glyphAdvance = (nextCodepoint != 0u) ? font->GetKerningAdvance(codepoint, nextCodepoint) : glyph->Advance;

            if (glyph->HasGeometry)
            {
                if (g_Data.TextIndexCount + 6 > Renderer2DData::kMaxIndices)
                {
                    FlushTextBatch();
                }

                const float left = penX + glyph->PlaneMin.x * atlasScale;
                const float right = penX + glyph->PlaneMax.x * atlasScale;
                const float bottom = penY + glyph->PlaneMin.y * atlasScale;
                const float top = penY + glyph->PlaneMax.y * atlasScale;

                const std::array<glm::vec4, 4> positions = {
                    glm::vec4(left,  bottom, 0.0f, 1.0f),
                    glm::vec4(right, bottom, 0.0f, 1.0f),
                    glm::vec4(right, top,    0.0f, 1.0f),
                    glm::vec4(left,  top,    0.0f, 1.0f),
                };

                const std::array<glm::vec2, 4> uvs = {
                    glm::vec2(glyph->AtlasMin.x, glyph->AtlasMin.y),
                    glm::vec2(glyph->AtlasMax.x, glyph->AtlasMin.y),
                    glm::vec2(glyph->AtlasMax.x, glyph->AtlasMax.y),
                    glm::vec2(glyph->AtlasMin.x, glyph->AtlasMax.y),
                };

                QuadVertex* vertex = g_Data.TextVertexBufferPtr;
                for (size_t vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
                {
                    vertex->Position = glm::vec3(transform * positions[vertexIndex]);
                    vertex->UV = uvs[vertexIndex];
                    vertex->Color = color;
                    vertex->TexIndex = texIndex;
                    ++vertex;
                }

                g_Data.TextVertexBufferPtr = vertex;
                g_Data.TextIndexCount += 6;
                g_Data.Stats.QuadCount += 1;
            }

            penX += glyphAdvance * atlasScale;
        }
    }

    void Renderer2D::FlushQuadBatch()
    {
        if (!g_Data.Initialized)
        {
            return;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(g_Data.VertexBufferPtr - g_Data.VertexBufferBase.get());
        if (g_Data.IndexCount == 0 || vertexCount == 0)
        {
            return;
        }

        // Re-fetch shader in case the asset became ready after initialization.
        if (g_Data.Material && !g_Data.ShaderProgram)
        {
            g_Data.ShaderProgram = g_Data.Material->GetShader();
            if (g_Data.ShaderProgram)
            {
                std::array<int, Renderer2DData::kMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < Renderer2DData::kMaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                g_Data.ShaderProgram->SetIntArray("u_Textures", samplers.data(), static_cast<uint32_t>(samplers.size()));
            }
        }

        if (!g_Data.ShaderProgram)
        {
            // Shader still loading; drop quads silently. Caller shows loading progress in UI.
            g_Data.IndexCount = 0;
            g_Data.VertexBufferPtr = g_Data.VertexBufferBase.get();
            g_Data.TextureSlotCount = 1;
            g_Data.TextureSlots[0] = g_Data.WhiteTexture;
            return;
        }

        auto& renderer = Renderer::GetInstance();

        // Upload CPU-staged vertices to the GPU buffer on the render thread.
        const uint32_t dataSizeBytes = vertexCount * static_cast<uint32_t>(sizeof(QuadVertex));
        void* uploadBytes = renderer.AllocateFrameUpload(dataSizeBytes, alignof(QuadVertex));
        if (!uploadBytes)
        {
            LT_CORE_WARN("Renderer2D::Flush: frame upload allocator out of memory (dropping {} quads)", g_Data.IndexCount / 6);
            g_Data.IndexCount = 0;
            g_Data.VertexBufferPtr = g_Data.VertexBufferBase.get();
            g_Data.TextureSlotCount = 1;
            g_Data.TextureSlots[0] = g_Data.WhiteTexture;
            return;
        }

        std::memcpy(uploadBytes, g_Data.VertexBufferBase.get(), dataSizeBytes);
        std::array<std::shared_ptr<Texture>, Renderer2DData::kMaxTextureSlots> textures{};
        for (uint32_t slot = 0; slot < g_Data.TextureSlotCount && slot < Renderer2DData::kMaxTextureSlots; ++slot)
        {
            // Move shared_ptr ownership into the flush packet so we avoid additional refcount churn.
            // `TextureSlots` are reset right after submission anyway.
            textures[slot] = std::move(g_Data.TextureSlots[slot]);
        }

        Renderer2DFlushCommand::KeepAlive keepAlive{};
        // These are long-lived renderer resources; copying the shared_ptr here is fine.
        // (Texture handles are moved above to avoid per-flush refcount churn.)
        keepAlive.VertexBufferHandle = g_Data.QuadVertexBuffer;
        keepAlive.VertexArrayHandle = g_Data.QuadVertexArray;
        keepAlive.ShaderProgramHandle = g_Data.ShaderProgram;
        keepAlive.TextureHandles = std::move(textures);

        renderer.SubmitCommandArena<Renderer2DFlushCommand>(
            std::move(keepAlive),
            uploadBytes,
            dataSizeBytes,
            g_Data.ViewProjection,
            g_Data.IndexCount,
            IndexType::UnsignedShort,
            g_Data.TextureSlotCount);

        g_Data.Stats.DrawCalls += 1;
        g_Data.Stats.Batches += 1;

        // Reset batch.
        g_Data.IndexCount = 0;
        g_Data.VertexBufferPtr = g_Data.VertexBufferBase.get();
        g_Data.TextureSlotCount = 1;
        g_Data.TextureSlots[0] = g_Data.WhiteTexture;
    }

    void Renderer2D::FlushTextBatch()
    {
        if (!g_Data.Initialized)
        {
            return;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(g_Data.TextVertexBufferPtr - g_Data.TextVertexBufferBase.get());
        if (g_Data.TextIndexCount == 0 || vertexCount == 0)
        {
            return;
        }

        if (g_Data.TextMaterial && !g_Data.TextShaderProgram)
        {
            g_Data.TextShaderProgram = g_Data.TextMaterial->GetShader();
            if (g_Data.TextShaderProgram)
            {
                std::array<int, Renderer2DData::kMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < Renderer2DData::kMaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                g_Data.TextShaderProgram->SetIntArray("u_Textures", samplers.data(), static_cast<uint32_t>(samplers.size()));
            }
        }

        if (!g_Data.TextShaderProgram)
        {
            g_Data.TextIndexCount = 0;
            g_Data.TextVertexBufferPtr = g_Data.TextVertexBufferBase.get();
            g_Data.TextTextureSlotCount = 1;
            g_Data.TextTextureSlots[0] = g_Data.WhiteTexture;
            return;
        }

        auto& renderer = Renderer::GetInstance();
        const uint32_t dataSizeBytes = vertexCount * static_cast<uint32_t>(sizeof(QuadVertex));
        void* uploadBytes = renderer.AllocateFrameUpload(dataSizeBytes, alignof(QuadVertex));
        if (!uploadBytes)
        {
            LT_CORE_WARN("Renderer2D::FlushTextBatch: frame upload allocator out of memory (dropping {} glyph quads)", g_Data.TextIndexCount / 6);
            g_Data.TextIndexCount = 0;
            g_Data.TextVertexBufferPtr = g_Data.TextVertexBufferBase.get();
            g_Data.TextTextureSlotCount = 1;
            g_Data.TextTextureSlots[0] = g_Data.WhiteTexture;
            return;
        }

        std::memcpy(uploadBytes, g_Data.TextVertexBufferBase.get(), dataSizeBytes);
        std::array<std::shared_ptr<Texture>, Renderer2DData::kMaxTextureSlots> textures{};
        for (uint32_t slot = 0; slot < g_Data.TextTextureSlotCount && slot < Renderer2DData::kMaxTextureSlots; ++slot)
        {
            textures[slot] = std::move(g_Data.TextTextureSlots[slot]);
        }

        Renderer2DFlushCommand::KeepAlive keepAlive{};
        keepAlive.VertexBufferHandle = g_Data.QuadVertexBuffer;
        keepAlive.VertexArrayHandle = g_Data.QuadVertexArray;
        keepAlive.ShaderProgramHandle = g_Data.TextShaderProgram;
        keepAlive.TextureHandles = std::move(textures);

        renderer.SubmitCommandArena<Renderer2DFlushCommand>(
            std::move(keepAlive),
            uploadBytes,
            dataSizeBytes,
            g_Data.ViewProjection,
            g_Data.TextIndexCount,
            IndexType::UnsignedShort,
            g_Data.TextTextureSlotCount);

        g_Data.Stats.DrawCalls += 1;
        g_Data.Stats.Batches += 1;

        g_Data.TextIndexCount = 0;
        g_Data.TextVertexBufferPtr = g_Data.TextVertexBufferBase.get();
        g_Data.TextTextureSlotCount = 1;
        g_Data.TextTextureSlots[0] = g_Data.WhiteTexture;
    }

    const Renderer2D::Statistics& Renderer2D::GetStatistics()
    {
        return g_Data.Stats;
    }

    const char* Renderer2D::GetDefaultShaderKey()
    {
        return "Assets/Shaders/Renderer2D_TexturedQuad.glsl";
    }

    bool Renderer2D::IsShaderReady()
    {
        if (!g_Data.Initialized)
        {
            return false;
        }
        if (g_Data.ShaderProgram)
        {
            return true;
        }
        if (g_Data.Material)
        {
            g_Data.ShaderProgram = g_Data.Material->GetShader();
            if (g_Data.ShaderProgram)
            {
                std::array<int, Renderer2DData::kMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < Renderer2DData::kMaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                g_Data.ShaderProgram->SetIntArray("u_Textures", samplers.data(), static_cast<uint32_t>(samplers.size()));
                return true;
            }
        }
        return false;
    }

    void Renderer2D::ResetStatistics()
    {
        g_Data.Stats.Reset();
    }
}

