#include "Graphics/Renderer2D.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAsset.h"

#include "Graphics/Buffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/RenderPipeline.h"
#include "Graphics/Renderer.h"
#include "Graphics/VertexArray.h"

#include <utf8.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kDefaultTexturedMaterialKey = "Assets/Materials/Renderer2D_TexturedQuad.material.json";
        constexpr const char* kDefaultTexturedShaderKey = "Assets/Shaders/Renderer2D_TexturedQuad.glsl";
        constexpr const char* kDefaultTextMaterialKey = "Assets/Materials/Renderer2D_MSDFText.material.json";
        constexpr const char* kDefaultTextShaderKey = "Assets/Shaders/Renderer2D_MSDFText.glsl";

        struct QuadVertex
        {
            glm::vec3 Position{0.0f};
            glm::vec2 UV{0.0f};
            glm::vec4 Color{1.0f};
            int32_t TexIndex = 0;
        };

        static glm::mat4 MakeQuadTransform2D(const glm::vec2& position, const glm::vec2& size)
        {
            glm::mat4 transform(1.0f);
            transform = glm::translate(transform, glm::vec3(position, 0.0f));
            transform = glm::scale(transform, glm::vec3(size, 1.0f));
            return transform;
        }

        constexpr size_t GetPipelineVariantIndex(bool depthTestEnabled)
        {
            return depthTestEnabled ? 1u : 0u;
        }

        void EnsureRenderer2DPipeline(std::array<std::shared_ptr<RenderPipeline>, 2>& pipelines,
                                      std::array<std::shared_ptr<Shader>, 2>& pipelineShaders,
                                      const std::shared_ptr<Shader>& shader,
                                      const BufferLayout& vertexLayout,
                                      const char* debugName,
                                      bool depthTestEnabled)
        {
            if (!shader)
                return;

            const size_t variantIndex = GetPipelineVariantIndex(depthTestEnabled);
            if (pipelines[variantIndex] && pipelineShaders[variantIndex] == shader)
                return;

            RenderPipelineDescriptor descriptor{};
            descriptor.DebugName = debugName ? debugName : "Renderer2D/Pipeline";
            descriptor.ShaderProgram = shader;
            descriptor.VertexLayout = vertexLayout;
            descriptor.Topology = PrimitiveTopology::Triangles;
            descriptor.BlendState.Enabled = true;
            descriptor.BlendState.SourceColorFactor = BlendFactor::SrcAlpha;
            descriptor.BlendState.DestinationColorFactor = BlendFactor::OneMinusSrcAlpha;
            descriptor.DepthStencilState.DepthTestEnabled = depthTestEnabled;
            descriptor.DepthStencilState.DepthWriteEnabled = depthTestEnabled;
            descriptor.DepthStencilState.DepthCompare = DepthTestFunc::LessEqual;
            descriptor.RasterState.CullEnabled = false;
            descriptor.RasterState.CullMode = CullFace::Back;
            descriptor.RasterState.FillMode = PolygonMode::Fill;

            pipelines[variantIndex] = RenderPipeline::Create(descriptor);
            pipelineShaders[variantIndex] = shader;
        }
    }

    struct Renderer2D::Impl
    {
        static constexpr uint32_t kMaxQuads = 10000;
        static constexpr uint32_t kMaxVertices = kMaxQuads * 4;
        static constexpr uint32_t kMaxIndices = kMaxQuads * 6;
        static constexpr uint32_t kCompileTimeMaxTextureSlots = 32;

        bool Initialized = false;
        uint32_t MaxTextureSlots = 16;

        std::shared_ptr<VertexArray> QuadVertexArray;
        std::shared_ptr<VertexBuffer> QuadVertexBuffer;
        std::shared_ptr<IndexBuffer> QuadIndexBuffer;

        Assets::MaterialAsset::Ptr Material;
        std::shared_ptr<Shader> ShaderProgram;
        Assets::MaterialAsset::Ptr TextMaterial;
        std::shared_ptr<Shader> TextShaderProgram;
        std::array<std::shared_ptr<RenderPipeline>, 2> QuadPipelines{};
        std::array<std::shared_ptr<Shader>, 2> QuadPipelineShaders{};
        std::array<std::shared_ptr<RenderPipeline>, 2> TextPipelines{};
        std::array<std::shared_ptr<Shader>, 2> TextPipelineShaders{};

        std::shared_ptr<Texture2D> WhiteTexture;
        std::array<std::shared_ptr<Texture2D>, kCompileTimeMaxTextureSlots> TextureSlots{};
        uint32_t TextureSlotCount = 0;
        bool SceneDepthTestEnabled = true;

        glm::mat4 ViewProjection{1.0f};

        std::unique_ptr<QuadVertex[]> VertexBufferBase;
        QuadVertex* VertexBufferPtr = nullptr;
        uint32_t IndexCount = 0;

        std::array<std::shared_ptr<Texture2D>, kCompileTimeMaxTextureSlots> TextTextureSlots{};
        uint32_t TextTextureSlotCount = 0;
        std::unique_ptr<QuadVertex[]> TextVertexBufferBase;
        QuadVertex* TextVertexBufferPtr = nullptr;
        uint32_t TextIndexCount = 0;

        Renderer2D::Statistics Stats{};
    };

    Renderer2D* Renderer2D::s_Default = nullptr;

    Renderer2D::Renderer2D()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    Renderer2D::~Renderer2D()
    {
        if (s_Default == this)
            s_Default = nullptr;
    }

    Renderer2D& Renderer2D::Default()
    {
        if (!s_Default)
        {
            static Renderer2D s_DefaultInstance;
            s_Default = &s_DefaultInstance;
        }
        return *s_Default;
    }

    void Renderer2D::EnsureInitialized()
    {
        if (!m_Impl->Initialized)
        {
            Initialize();
        }
    }

    void Renderer2D::Initialize()
    {
        auto& d = *m_Impl;

        if (d.Initialized)
        {
            return;
        }

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
        {
            LT_CORE_WARN("Renderer2D::Initialize called before Renderer is initialized (skipping)");
            return;
        }

        d.QuadVertexArray = VertexArray::Create();
        d.QuadVertexBuffer = VertexBuffer::Create(Impl::kMaxVertices * static_cast<uint32_t>(sizeof(QuadVertex)));

        d.QuadVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_UV" },
            { ShaderDataType::Float4, "a_Color" },
            { ShaderDataType::Int,    "a_TexIndex" }
        });
        d.QuadVertexArray->AddVertexBuffer(d.QuadVertexBuffer);

        std::vector<uint16_t> indices;
        indices.resize(Impl::kMaxIndices);
        uint16_t offset = 0;
        for (uint32_t i = 0; i < Impl::kMaxIndices; i += 6)
        {
            indices[i + 0] = static_cast<uint16_t>(offset + 0);
            indices[i + 1] = static_cast<uint16_t>(offset + 1);
            indices[i + 2] = static_cast<uint16_t>(offset + 2);

            indices[i + 3] = static_cast<uint16_t>(offset + 2);
            indices[i + 4] = static_cast<uint16_t>(offset + 3);
            indices[i + 5] = static_cast<uint16_t>(offset + 0);

            offset = static_cast<uint16_t>(offset + 4);
        }

        d.QuadIndexBuffer = IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size()));
        d.QuadVertexArray->SetIndexBuffer(d.QuadIndexBuffer);

        if (auto* ctx = renderer.GetGraphicsContext())
        {
            const int32_t hwMax = ctx->GetMaxTextureImageUnits();
            d.MaxTextureSlots = static_cast<uint32_t>(
                std::clamp(hwMax, int32_t(1), static_cast<int32_t>(Impl::kCompileTimeMaxTextureSlots)));
        }

        const uint32_t whitePixelRGBA8 = 0xFFFFFFFFu;
        TextureSpecification whiteSpec{};
        whiteSpec.GenerateMipmaps = false;
        whiteSpec.MinFilter = TextureFilter::Nearest;
        whiteSpec.MagFilter = TextureFilter::Nearest;
        whiteSpec.WrapU = TextureWrap::ClampToEdge;
        whiteSpec.WrapV = TextureWrap::ClampToEdge;
        d.WhiteTexture = Texture2D::CreateFromRGBA8(1, 1, &whitePixelRGBA8, whiteSpec);

        d.Material = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(kDefaultTexturedMaterialKey);
        if (d.Material)
        {
            d.ShaderProgram = d.Material->GetShader();
        }
        else
        {
            LT_CORE_WARN("Renderer2D: default material asset missing; falling back to shader asset '{}'", kDefaultTexturedShaderKey);
            auto fallbackShaderAsset = Assets::ShaderAsset::LoadBlocking(kDefaultTexturedShaderKey);
            if (fallbackShaderAsset)
                d.ShaderProgram = fallbackShaderAsset->GetShader();
        }
        if (!d.ShaderProgram)
        {
            LT_CORE_ERROR("Renderer2D: failed to load default 2D shader (material='{}', shader='{}')",
                          kDefaultTexturedMaterialKey, kDefaultTexturedShaderKey);
            return;
        }

        {
            std::array<int, Impl::kCompileTimeMaxTextureSlots> samplers{};
            for (uint32_t i = 0; i < d.MaxTextureSlots; ++i)
            {
                samplers[i] = static_cast<int>(i);
            }
            d.ShaderProgram->SetIntArray("u_Textures", samplers.data(), d.MaxTextureSlots);
        }

        d.TextMaterial = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(kDefaultTextMaterialKey);
        if (d.TextMaterial)
        {
            d.TextShaderProgram = d.TextMaterial->GetShader();
        }
        else
        {
            LT_CORE_WARN("Renderer2D: text material asset missing; falling back to shader asset '{}'", kDefaultTextShaderKey);
            auto fallbackTextShaderAsset = Assets::ShaderAsset::LoadBlocking(kDefaultTextShaderKey);
            if (fallbackTextShaderAsset)
                d.TextShaderProgram = fallbackTextShaderAsset->GetShader();
        }

        if (d.TextShaderProgram)
        {
            std::array<int, Impl::kCompileTimeMaxTextureSlots> samplers{};
            for (uint32_t i = 0; i < d.MaxTextureSlots; ++i)
            {
                samplers[i] = static_cast<int>(i);
            }
            d.TextShaderProgram->SetIntArray("u_Textures", samplers.data(), d.MaxTextureSlots);
        }
        else
        {
            LT_CORE_WARN("Renderer2D: text shader unavailable (material='{}', shader='{}'); text rendering disabled",
                         kDefaultTextMaterialKey, kDefaultTextShaderKey);
        }

        d.VertexBufferBase = std::make_unique<QuadVertex[]>(Impl::kMaxVertices);
        d.VertexBufferPtr = d.VertexBufferBase.get();
        d.TextVertexBufferBase = std::make_unique<QuadVertex[]>(Impl::kMaxVertices);
        d.TextVertexBufferPtr = d.TextVertexBufferBase.get();
        d.Initialized = true;

        if (!s_Default)
            s_Default = this;

        LT_CORE_INFO("Renderer2D initialized (MaxQuadsPerBatch={}, MaxTextureSlotsPerBatch={})",
                     Impl::kMaxQuads, d.MaxTextureSlots);
    }

    void Renderer2D::Shutdown()
    {
        *m_Impl = {};

        if (s_Default == this)
            s_Default = nullptr;
    }

    void Renderer2D::BeginScene(const Camera& camera)
    {
        EnsureInitialized();
        if (!m_Impl->Initialized)
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
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return;
        }

        d.ViewProjection = viewProjection;
        d.IndexCount = 0;
        d.VertexBufferPtr = d.VertexBufferBase.get();
        d.TextIndexCount = 0;
        d.TextVertexBufferPtr = d.TextVertexBufferBase.get();

        d.TextureSlotCount = 1;
        d.TextureSlots[0] = d.WhiteTexture;
        d.TextTextureSlotCount = 1;
        d.TextTextureSlots[0] = d.WhiteTexture;
        d.SceneDepthTestEnabled = enableDepthTest;

        EnsureRenderer2DPipeline(d.QuadPipelines,
                                 d.QuadPipelineShaders,
                                 d.ShaderProgram,
                                 d.QuadVertexBuffer->GetLayout(),
                                 "Renderer2D/Quad",
                                 d.SceneDepthTestEnabled);
        EnsureRenderer2DPipeline(d.TextPipelines,
                                 d.TextPipelineShaders,
                                 d.TextShaderProgram,
                                 d.QuadVertexBuffer->GetLayout(),
                                 "Renderer2D/Text",
                                 d.SceneDepthTestEnabled);
    }

    void Renderer2D::EndScene()
    {
        if (!m_Impl->Initialized)
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
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return;
        }

        if (d.IndexCount + 6 > Impl::kMaxIndices)
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

        constexpr int32_t texIndex = 0;

        QuadVertex* v = d.VertexBufferPtr;
        for (size_t i = 0; i < 4; ++i)
        {
            v->Position = glm::vec3(transform * kQuadPositions[i]);
            v->UV = kUVs[i];
            v->Color = color;
            v->TexIndex = texIndex;
            ++v;
        }
        d.VertexBufferPtr = v;

        d.IndexCount += 6;
        d.Stats.QuadCount += 1;
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
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return;
        }

        std::shared_ptr<Texture2D> tex = texture ? texture->GetTexture() : nullptr;
        if (!tex)
        {
            DrawQuad(transform, tintColor);
            return;
        }

        if (d.IndexCount + 6 > Impl::kMaxIndices)
        {
            FlushQuadBatch();
        }

        int32_t texIndex = -1;
        for (uint32_t i = 0; i < d.TextureSlotCount; ++i)
        {
            if (d.TextureSlots[i] == tex)
            {
                texIndex = static_cast<int32_t>(i);
                break;
            }
        }

        if (texIndex < 0)
        {
            if (d.TextureSlotCount >= d.MaxTextureSlots)
            {
                FlushQuadBatch();
            }

            texIndex = static_cast<int32_t>(d.TextureSlotCount);
            d.TextureSlots[d.TextureSlotCount] = tex;
            d.TextureSlotCount++;
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

        QuadVertex* v = d.VertexBufferPtr;
        for (size_t i = 0; i < 4; ++i)
        {
            v->Position = glm::vec3(transform * kQuadPositions[i]);
            v->UV = uvs[i];
            v->Color = tintColor;
            v->TexIndex = texIndex;
            ++v;
        }
        d.VertexBufferPtr = v;

        d.IndexCount += 6;
        d.Stats.QuadCount += 1;
    }

    void Renderer2D::DrawText(const glm::mat4& transform, const std::string& text, const Font::Ptr& font, float fontSize, const glm::vec4& color)
    {
        EnsureInitialized();
        auto& d = *m_Impl;
        if (!d.Initialized || !font || text.empty())
        {
            return;
        }

        std::shared_ptr<Texture2D> atlasTexture = font->GetAtlasTexture();
        if (!atlasTexture)
        {
            return;
        }

        int32_t texIndex = -1;
        for (uint32_t i = 0; i < d.TextTextureSlotCount; ++i)
        {
            if (d.TextTextureSlots[i] == atlasTexture)
            {
                texIndex = static_cast<int32_t>(i);
                break;
            }
        }

        if (texIndex < 0)
        {
            if (d.TextTextureSlotCount >= d.MaxTextureSlots)
            {
                FlushTextBatch();
            }
            texIndex = static_cast<int32_t>(d.TextTextureSlotCount);
            d.TextTextureSlots[d.TextTextureSlotCount] = atlasTexture;
            ++d.TextTextureSlotCount;
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
                if (d.TextIndexCount + 6 > Impl::kMaxIndices)
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

                QuadVertex* vertex = d.TextVertexBufferPtr;
                for (size_t vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
                {
                    vertex->Position = glm::vec3(transform * positions[vertexIndex]);
                    vertex->UV = uvs[vertexIndex];
                    vertex->Color = color;
                    vertex->TexIndex = texIndex;
                    ++vertex;
                }

                d.TextVertexBufferPtr = vertex;
                d.TextIndexCount += 6;
                d.Stats.QuadCount += 1;
            }

            penX += glyphAdvance * atlasScale;
        }
    }

    void Renderer2D::FlushQuadBatch()
    {
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(d.VertexBufferPtr - d.VertexBufferBase.get());
        if (d.IndexCount == 0 || vertexCount == 0)
        {
            return;
        }

        if (d.Material && !d.ShaderProgram)
        {
            d.ShaderProgram = d.Material->GetShader();
            if (d.ShaderProgram)
            {
                std::array<int, Impl::kCompileTimeMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < d.MaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                d.ShaderProgram->SetIntArray("u_Textures", samplers.data(), d.MaxTextureSlots);
            }
        }

        if (!d.ShaderProgram)
        {
            d.IndexCount = 0;
            d.VertexBufferPtr = d.VertexBufferBase.get();
            d.TextureSlotCount = 1;
            d.TextureSlots[0] = d.WhiteTexture;
            return;
        }

        auto& renderer = Renderer::GetInstance();
        EnsureRenderer2DPipeline(d.QuadPipelines,
                                 d.QuadPipelineShaders,
                                 d.ShaderProgram,
                                 d.QuadVertexBuffer->GetLayout(),
                                 "Renderer2D/Quad",
                                 d.SceneDepthTestEnabled);

        const auto& quadPipeline = d.QuadPipelines[GetPipelineVariantIndex(d.SceneDepthTestEnabled)];
        if (quadPipeline)
        {
            renderer.SubmitCommandArena<BindRenderPipelineCommand>(quadPipeline);
        }
        else
        {
            renderer.SubmitCommandArena<SetBlendModeCommand>(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, true);
            renderer.SubmitCommandArena<SetDepthTestCommand>(d.SceneDepthTestEnabled);
            renderer.SubmitCommandArena<SetCullFaceCommand>(false);
        }

        const uint32_t dataSizeBytes = vertexCount * static_cast<uint32_t>(sizeof(QuadVertex));
        void* uploadBytes = renderer.AllocateFrameUpload(dataSizeBytes, alignof(QuadVertex));
        if (!uploadBytes)
        {
            LT_CORE_WARN("Renderer2D::Flush: frame upload allocator out of memory (dropping {} quads)", d.IndexCount / 6);
            d.IndexCount = 0;
            d.VertexBufferPtr = d.VertexBufferBase.get();
            d.TextureSlotCount = 1;
            d.TextureSlots[0] = d.WhiteTexture;
            return;
        }

        std::memcpy(uploadBytes, d.VertexBufferBase.get(), dataSizeBytes);
        std::array<std::shared_ptr<Texture>, Impl::kCompileTimeMaxTextureSlots> textures{};
        for (uint32_t slot = 0; slot < d.TextureSlotCount && slot < d.MaxTextureSlots; ++slot)
        {
            textures[slot] = std::move(d.TextureSlots[slot]);
        }

        Renderer2DFlushCommand::KeepAlive keepAlive{};
        keepAlive.VertexBufferHandle = d.QuadVertexBuffer;
        keepAlive.VertexArrayHandle = d.QuadVertexArray;
        keepAlive.ShaderProgramHandle = d.ShaderProgram;
        keepAlive.TextureHandles = std::move(textures);

        renderer.SubmitCommandArena<Renderer2DFlushCommand>(
            std::move(keepAlive),
            uploadBytes,
            dataSizeBytes,
            d.ViewProjection,
            d.IndexCount,
            IndexType::UnsignedShort,
            d.TextureSlotCount);

        d.Stats.DrawCalls += 1;
        d.Stats.Batches += 1;

        d.IndexCount = 0;
        d.VertexBufferPtr = d.VertexBufferBase.get();
        d.TextureSlotCount = 1;
        d.TextureSlots[0] = d.WhiteTexture;
    }

    void Renderer2D::FlushTextBatch()
    {
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return;
        }

        const uint32_t vertexCount = static_cast<uint32_t>(d.TextVertexBufferPtr - d.TextVertexBufferBase.get());
        if (d.TextIndexCount == 0 || vertexCount == 0)
        {
            return;
        }

        if (d.TextMaterial && !d.TextShaderProgram)
        {
            d.TextShaderProgram = d.TextMaterial->GetShader();
            if (d.TextShaderProgram)
            {
                std::array<int, Impl::kCompileTimeMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < d.MaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                d.TextShaderProgram->SetIntArray("u_Textures", samplers.data(), d.MaxTextureSlots);
            }
        }

        if (!d.TextShaderProgram)
        {
            d.TextIndexCount = 0;
            d.TextVertexBufferPtr = d.TextVertexBufferBase.get();
            d.TextTextureSlotCount = 1;
            d.TextTextureSlots[0] = d.WhiteTexture;
            return;
        }

        auto& renderer = Renderer::GetInstance();
        EnsureRenderer2DPipeline(d.TextPipelines,
                                 d.TextPipelineShaders,
                                 d.TextShaderProgram,
                                 d.QuadVertexBuffer->GetLayout(),
                                 "Renderer2D/Text",
                                 d.SceneDepthTestEnabled);

        const auto& textPipeline = d.TextPipelines[GetPipelineVariantIndex(d.SceneDepthTestEnabled)];
        if (textPipeline)
        {
            renderer.SubmitCommandArena<BindRenderPipelineCommand>(textPipeline);
        }
        else
        {
            renderer.SubmitCommandArena<SetBlendModeCommand>(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, true);
            renderer.SubmitCommandArena<SetDepthTestCommand>(d.SceneDepthTestEnabled);
            renderer.SubmitCommandArena<SetCullFaceCommand>(false);
        }

        const uint32_t dataSizeBytes = vertexCount * static_cast<uint32_t>(sizeof(QuadVertex));
        void* uploadBytes = renderer.AllocateFrameUpload(dataSizeBytes, alignof(QuadVertex));
        if (!uploadBytes)
        {
            LT_CORE_WARN("Renderer2D::FlushTextBatch: frame upload allocator out of memory (dropping {} glyph quads)", d.TextIndexCount / 6);
            d.TextIndexCount = 0;
            d.TextVertexBufferPtr = d.TextVertexBufferBase.get();
            d.TextTextureSlotCount = 1;
            d.TextTextureSlots[0] = d.WhiteTexture;
            return;
        }

        std::memcpy(uploadBytes, d.TextVertexBufferBase.get(), dataSizeBytes);
        std::array<std::shared_ptr<Texture>, Impl::kCompileTimeMaxTextureSlots> textures{};
        for (uint32_t slot = 0; slot < d.TextTextureSlotCount && slot < d.MaxTextureSlots; ++slot)
        {
            textures[slot] = std::move(d.TextTextureSlots[slot]);
        }

        Renderer2DFlushCommand::KeepAlive keepAlive{};
        keepAlive.VertexBufferHandle = d.QuadVertexBuffer;
        keepAlive.VertexArrayHandle = d.QuadVertexArray;
        keepAlive.ShaderProgramHandle = d.TextShaderProgram;
        keepAlive.TextureHandles = std::move(textures);

        renderer.SubmitCommandArena<Renderer2DFlushCommand>(
            std::move(keepAlive),
            uploadBytes,
            dataSizeBytes,
            d.ViewProjection,
            d.TextIndexCount,
            IndexType::UnsignedShort,
            d.TextTextureSlotCount);

        d.Stats.DrawCalls += 1;
        d.Stats.Batches += 1;

        d.TextIndexCount = 0;
        d.TextVertexBufferPtr = d.TextVertexBufferBase.get();
        d.TextTextureSlotCount = 1;
        d.TextTextureSlots[0] = d.WhiteTexture;
    }

    const Renderer2D::Statistics& Renderer2D::GetStatistics() const
    {
        return m_Impl->Stats;
    }

    const char* Renderer2D::GetDefaultShaderKey()
    {
        return "Assets/Shaders/Renderer2D_TexturedQuad.glsl";
    }

    bool Renderer2D::IsShaderReady()
    {
        auto& d = *m_Impl;
        if (!d.Initialized)
        {
            return false;
        }
        if (d.ShaderProgram)
        {
            return true;
        }
        if (d.Material)
        {
            d.ShaderProgram = d.Material->GetShader();
            if (d.ShaderProgram)
            {
                std::array<int, Impl::kCompileTimeMaxTextureSlots> samplers{};
                for (uint32_t i = 0; i < d.MaxTextureSlots; ++i)
                {
                    samplers[i] = static_cast<int>(i);
                }
                d.ShaderProgram->SetIntArray("u_Textures", samplers.data(), d.MaxTextureSlots);
                return true;
            }
        }
        return false;
    }

    void Renderer2D::ResetStatistics()
    {
        m_Impl->Stats.Reset();
    }
}
