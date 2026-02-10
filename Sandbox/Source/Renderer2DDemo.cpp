#include "Renderer2DDemo.h"

#include <cmath>

namespace Limitless
{
    void Renderer2DDemo::Initialize(uint32_t viewportWidthPixels, uint32_t viewportHeightPixels)
    {
        (void)viewportWidthPixels;
        (void)viewportHeightPixels;

        Renderer2D::Initialize();

        // Use existing TextureAsset type (async-ready, hot-reload friendly).
        TextureSpecification checkerSpec{};
        checkerSpec.GenerateMipmaps = true;
        checkerSpec.MinFilter = TextureFilter::Linear;
        checkerSpec.MagFilter = TextureFilter::Linear;
        checkerSpec.WrapU = TextureWrap::ClampToEdge;
        checkerSpec.WrapV = TextureWrap::ClampToEdge;

        m_CheckerTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>("Assets/Textures/Checker.ppm", checkerSpec);
        if (!m_CheckerTexture)
        {
            LT_CORE_ERROR("Renderer2DDemo: failed to load checker texture asset");
        }

        // Load your new JPG for a single showcase quad.
        // Keep sampler settings consistent with the checker texture for now.
        m_SissyTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>("Assets/Textures/sissy.jpg", checkerSpec);
        if (!m_SissyTexture)
        {
            LT_CORE_ERROR("Renderer2DDemo: failed to load sissy texture asset");
        }
    }

    void Renderer2DDemo::Shutdown()
    {
        m_CheckerTexture.reset();
        m_SissyTexture.reset();
        m_TimeSeconds = 0.0f;
        m_StatsLogAccumulatorSeconds = 0.0f;
        m_LoggedReadyOnce = false;
    }

    void Renderer2DDemo::Update(float deltaTime)
    {
        m_TimeSeconds += deltaTime;

        EnsureAssetsReady();

        // Log stats at a low cadence so they stay readable.
        m_StatsLogAccumulatorSeconds += deltaTime;
        if (m_StatsLogAccumulatorSeconds >= 1.0f)
        {
            m_StatsLogAccumulatorSeconds = 0.0f;
            const auto& stats = Renderer2D::GetStatistics();
            LT_INFO("Renderer2D Stats: DrawCalls={}, Batches={}, Quads={}", stats.DrawCalls, stats.Batches, stats.QuadCount);
            Renderer2D::ResetStatistics();
        }
    }

    void Renderer2DDemo::Render(const Camera& camera) const
    {
        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
        {
            return;
        }

        ClearCommand::ClearFlags flags;
        flags.color = true;
        flags.depth = true;
        flags.stencil = false;

        const float r = 0.08f;
        const float g = 0.08f;
        const float b = 0.10f;
        const float a = 1.0f;
        renderer.SubmitCommand(std::make_unique<ClearCommand>(flags, r, g, b, a));

        Renderer2D::BeginScene(camera);

        // A small grid that should batch into a single draw call (same texture).
        // Keep the scene deterministic so draw-call/batch counts are stable frame-to-frame.
        const int gridWidth = 20;
        const int gridHeight = 12;
        const float spacing = 0.55f;

        for (int y = 0; y < gridHeight; ++y)
        {
            for (int x = 0; x < gridWidth; ++x)
            {
                // These world units work in both orthographic and perspective cameras.
                // For perspective cameras, you can fly the editor camera and see the grid on the Z=0 plane.
                const glm::vec2 pos = glm::vec2(-5.0f + x * spacing, -3.0f + y * spacing);
                const glm::vec2 size = glm::vec2(0.5f, 0.5f);

                // Gentle tint variation so you can visually confirm it is not a single quad.
                const float t = 0.5f + 0.5f * std::sin(m_TimeSeconds + (x + y) * 0.15f);
                const glm::vec4 tint = glm::vec4(0.6f + 0.4f * t, 0.6f, 0.9f, 1.0f);

                // Use the new JPG on one quad as a visual sanity check.
                if (x == gridWidth / 2 && y == gridHeight / 2)
                {
                    Renderer2D::DrawQuad(pos, size, m_SissyTexture, glm::vec4(1.0f));
                }
                else
                {
                    Renderer2D::DrawQuad(pos, size, m_CheckerTexture, tint);
                }
            }
        }

        // A couple of solid-color quads.
        Renderer2D::DrawQuad(glm::vec2(1.5f, 1.5f), glm::vec2(1.25f, 0.35f), glm::vec4(0.1f, 0.9f, 0.2f, 1.0f));
        Renderer2D::DrawQuad(glm::vec2(1.5f, 1.0f), glm::vec2(1.25f, 0.35f), glm::vec4(0.9f, 0.2f, 0.2f, 0.8f));

        Renderer2D::EndScene();
    }

    void Renderer2DDemo::EnsureAssetsReady()
    {
        if (m_LoggedReadyOnce)
        {
            return;
        }

        const bool checkerReady = m_CheckerTexture && m_CheckerTexture->GetTexture() != nullptr;
        const bool sissyReady = m_SissyTexture && m_SissyTexture->GetTexture() != nullptr;
        if (checkerReady && sissyReady)
        {
            m_LoggedReadyOnce = true;
            LT_INFO("Renderer2DDemo: assets ready (checkerTextureId={}, sissyTextureId={})",
                    m_CheckerTexture->GetTexture()->GetRendererID(),
                    m_SissyTexture->GetTexture()->GetRendererID());
        }
    }
}

