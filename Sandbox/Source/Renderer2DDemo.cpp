#include "Renderer2DDemo.h"

#include <cmath>

namespace Limitless
{
    namespace
    {
        static glm::vec4 MakeDeterministicTint(uint32_t x, uint32_t y)
        {
            // Tiny hash to produce stable, human-checkable variation without RNG.
            uint32_t h = x * 73856093u ^ y * 19349663u ^ 0x9E3779B9u;
            h ^= (h >> 16);
            const float r = 0.55f + 0.45f * (static_cast<float>((h >> 0) & 255u) / 255.0f);
            const float g = 0.55f + 0.45f * (static_cast<float>((h >> 8) & 255u) / 255.0f);
            const float b = 0.55f + 0.45f * (static_cast<float>((h >> 16) & 255u) / 255.0f);
            return glm::vec4(r, g, b, 1.0f);
        }
    }

    void Renderer2DDemo::Initialize(uint32_t viewportWidthPixels, uint32_t viewportHeightPixels)
    {
        (void)viewportWidthPixels;
        (void)viewportHeightPixels;

        Renderer2D::Initialize();

        // Use existing TextureAsset type (async-ready, hot-reload friendly).
        TextureSpecification checkerSpec{};
        // For a debug checker, prefer crisp texels over stability.
        // (Linear+mips is "correct" but makes small checker textures look smeared.)
        checkerSpec.GenerateMipmaps = false;
        checkerSpec.MinFilter = TextureFilter::Nearest;
        checkerSpec.MagFilter = TextureFilter::Nearest;
        checkerSpec.WrapU = TextureWrap::Repeat;
        checkerSpec.WrapV = TextureWrap::Repeat;

        m_CheckerTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>("Assets/Textures/Checker.ppm", checkerSpec);
        if (!m_CheckerTexture)
        {
            LT_CORE_ERROR("Renderer2DDemo: failed to load checker texture asset");
        }

        // Load your new JPG for a single showcase quad.
        // For photographs, prefer stability and smoother reconstruction.
        TextureSpecification photoSpec{};
        photoSpec.GenerateMipmaps = true;
        photoSpec.MinFilter = TextureFilter::Linear;
        photoSpec.MagFilter = TextureFilter::Linear;
        photoSpec.WrapU = TextureWrap::ClampToEdge;
        photoSpec.WrapV = TextureWrap::ClampToEdge;

        m_SissyTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>("Assets/Textures/sissy.jpg", photoSpec);
        if (!m_SissyTexture)
        {
            LT_CORE_ERROR("Renderer2DDemo: failed to load sissy texture asset");
        }
    }

    void Renderer2DDemo::SetStressTestSettings(const StressTestSettings& settings)
    {
        m_StressTest = settings;

        // Keep values sane so a bad hot-reload doesn't allocate extreme work.
        m_StressTest.GridWidth = std::max(1u, m_StressTest.GridWidth);
        m_StressTest.GridHeight = std::max(1u, m_StressTest.GridHeight);
        m_StressTest.Spacing = std::max(0.01f, m_StressTest.Spacing);
        m_StressTest.AlternateTextureStride = std::max(1u, m_StressTest.AlternateTextureStride);
    }

    void Renderer2DDemo::ApplyStressPreset(uint32_t presetIndex)
    {
        // These values are picked so you can quickly hit:
        // - below one batch (sanity)
        // - around one batch (10k)
        // - multiple batches
        // without needing to edit code.
        StressTestSettings preset = m_StressTest;
        preset.Enabled = true;

        switch (presetIndex)
        {
            case 1:
                preset.GridWidth = 48;
                preset.GridHeight = 30; // 1,440
                preset.Spacing = 0.20f;
                break;
            case 2:
                preset.GridWidth = 125;
                preset.GridHeight = 80; // 10,000
                preset.Spacing = 0.11f;
                break;
            case 3:
                preset.GridWidth = 200;
                preset.GridHeight = 100; // 20,000
                preset.Spacing = 0.10f;
                break;
            case 4:
                preset.GridWidth = 250;
                preset.GridHeight = 200; // 50,000
                preset.Spacing = 0.08f;
                break;
            default:
                // Unknown preset -> no-op.
                return;
        }

        SetStressTestSettings(preset);
        LT_INFO("Renderer2DDemo stress preset {} applied (Grid={}x{}, AlternateTextures={}, Stride={}, Spacing={})",
                presetIndex, preset.GridWidth, preset.GridHeight, preset.AlternateTextures, preset.AlternateTextureStride, preset.Spacing);
    }

    void Renderer2DDemo::Shutdown()
    {
        m_CheckerTexture.reset();
        m_SissyTexture.reset();
        m_TimeSeconds = 0.0f;
        m_StatsLogAccumulatorSeconds = 0.0f;
        m_FramesSinceLastStatsLog = 0;
        m_LoggedReadyOnce = false;
    }

    void Renderer2DDemo::Update(float deltaTime)
    {
        m_TimeSeconds += deltaTime;
        ++m_FramesSinceLastStatsLog;

        EnsureAssetsReady();

        // Log stats at a low cadence so they stay readable.
        m_StatsLogAccumulatorSeconds += deltaTime;
        if (m_StatsLogAccumulatorSeconds >= 1.0f)
        {
            const float intervalSeconds = m_StatsLogAccumulatorSeconds;
            m_StatsLogAccumulatorSeconds = 0.0f;

            const uint64_t frames = m_FramesSinceLastStatsLog;
            m_FramesSinceLastStatsLog = 0;

            const auto& stats = Renderer2D::GetStatistics();

            const float fps = (intervalSeconds > 0.0f) ? (static_cast<float>(frames) / intervalSeconds) : 0.0f;
            const float quadsPerFrame = (frames > 0) ? (static_cast<float>(stats.QuadCount) / static_cast<float>(frames)) : 0.0f;
            const float drawCallsPerFrame = (frames > 0) ? (static_cast<float>(stats.DrawCalls) / static_cast<float>(frames)) : 0.0f;
            const float batchesPerFrame = (frames > 0) ? (static_cast<float>(stats.Batches) / static_cast<float>(frames)) : 0.0f;

            LT_INFO("Renderer2D Stats: FPS={:.1f}, DrawCalls={} ({:.2f}/frame), Batches={} ({:.2f}/frame), Quads={} ({:.0f}/frame)",
                    fps, stats.DrawCalls, drawCallsPerFrame, stats.Batches, batchesPerFrame, stats.QuadCount, quadsPerFrame);

            const auto resourceStats = Renderer::GetInstance().GetLastFrameResourceQueueStatistics();
            LT_INFO("Resource Queues: PrimaryProcessed={} SharedProcessed={} (PrimaryApproxSize={} SharedApproxSize={}) Totals: Primary(Sub={}, Proc={}) Shared(Sub={}, Proc={})",
                    resourceStats.PrimaryProcessedLastFrame,
                    resourceStats.SharedProcessedLastFrame,
                    resourceStats.PrimaryApproxSize,
                    resourceStats.SharedApproxSize,
                    resourceStats.PrimaryTotalSubmitted,
                    resourceStats.PrimaryTotalProcessed,
                    resourceStats.SharedTotalSubmitted,
                    resourceStats.SharedTotalProcessed);

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

        if (m_StressTest.Enabled)
        {
            // Performance stress test:
            // - GridWidth * GridHeight quads on Z=0 plane.
            // - Uses either one texture (best-case batching) or periodic texture swaps (stress-case batching).
            const uint32_t gridWidth = m_StressTest.GridWidth;
            const uint32_t gridHeight = m_StressTest.GridHeight;
            const float spacing = m_StressTest.Spacing;
            const glm::vec2 quadSize = glm::vec2(spacing * 0.92f, spacing * 0.92f);

            // Center the grid around the origin so it behaves well with either camera type.
            const float totalWidth = (gridWidth > 1) ? (static_cast<float>(gridWidth - 1) * spacing) : 0.0f;
            const float totalHeight = (gridHeight > 1) ? (static_cast<float>(gridHeight - 1) * spacing) : 0.0f;
            const float startX = -0.5f * totalWidth;
            const float startY = -0.5f * totalHeight;

            const uint32_t centerX = gridWidth / 2;
            const uint32_t centerY = gridHeight / 2;
            const uint32_t stride = std::max(1u, m_StressTest.AlternateTextureStride);

            for (uint32_t y = 0; y < gridHeight; ++y)
            {
                for (uint32_t x = 0; x < gridWidth; ++x)
                {
                    const glm::vec2 pos = glm::vec2(startX + static_cast<float>(x) * spacing,
                                                    startY + static_cast<float>(y) * spacing);

                    // Put the JPG at the center as a persistent “orientation / sampling” sanity check.
                    if (x == centerX && y == centerY)
                    {
                        Renderer2D::DrawQuad(pos, quadSize, m_SissyTexture, glm::vec4(1.0f));
                        continue;
                    }

                    // Stable tint so it’s easy to see the grid and detect if quads are collapsing.
                    const glm::vec4 tint = MakeDeterministicTint(x, y);

                    if (m_StressTest.AlternateTextures)
                    {
                        // Swap texture periodically to intentionally create more batches.
                        // This is a good way to see how much state changes cost on your machine.
                        const uint32_t linearIndex = x + y * gridWidth;
                        const bool usePhoto = ((linearIndex / stride) & 1u) != 0u;
                        Renderer2D::DrawQuad(pos, quadSize, usePhoto ? m_SissyTexture : m_CheckerTexture, tint);
                    }
                    else
                    {
                        Renderer2D::DrawQuad(pos, quadSize, m_CheckerTexture, tint);
                    }
                }
            }
        }
        else
        {
            // Minimal scene for quick sanity checks.
            Renderer2D::DrawQuad(glm::vec2(-0.6f, 0.0f), glm::vec2(1.0f, 1.0f), m_CheckerTexture, glm::vec4(1.0f));
            Renderer2D::DrawQuad(glm::vec2(0.6f, 0.0f), glm::vec2(1.0f, 1.0f), m_SissyTexture, glm::vec4(1.0f));
            Renderer2D::DrawQuad(glm::vec2(0.0f, -1.25f), glm::vec2(2.0f, 0.35f), glm::vec4(0.15f, 0.85f, 0.25f, 1.0f));
        }

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

            // One-time startup resource summary (useful because most resource work happens before steady-state frames).
            const auto resourceStats = Renderer::GetInstance().GetLastFrameResourceQueueStatistics();
            LT_INFO("Startup Resource Totals: Primary(Sub={}, Proc={}) Shared(Sub={}, Proc={})",
                    resourceStats.PrimaryTotalSubmitted,
                    resourceStats.PrimaryTotalProcessed,
                    resourceStats.SharedTotalSubmitted,
                    resourceStats.SharedTotalProcessed);
        }
    }
}

