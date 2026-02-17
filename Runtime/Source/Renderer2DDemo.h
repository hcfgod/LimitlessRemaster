#pragma once

#include "Limitless.h"

#include "Assets/TextureAssetImporter.h"

#include <memory>

namespace Limitless
{
    class Renderer2DDemo final
    {
    public:
        struct StressTestSettings
        {
            // Controls a deterministic quad grid rendered on the Z=0 plane.
            // With a perspective editor camera, you can fly around and visually confirm batching behavior.
            bool Enabled = true;

            // Total quads rendered is GridWidth * GridHeight.
            // Renderer2D batches up to 10,000 quads per flush, so values above that will force multiple batches.
            uint32_t GridWidth = 160;
            uint32_t GridHeight = 90;

            // World-unit spacing between quads (and implicitly the quad size).
            float Spacing = 0.11f;

            // When enabled, periodically swap between two textures within the grid.
            // This intentionally creates more batches (stress-testing state changes).
            bool AlternateTextures = false;
            uint32_t AlternateTextureStride = 32;
        };

        Renderer2DDemo() = default;
        ~Renderer2DDemo() = default;

        void Initialize(uint32_t viewportWidthPixels, uint32_t viewportHeightPixels);
        void Shutdown();

        void Update(float deltaTime);
        void Render(const Camera& camera) const;

        void SetStressTestSettings(const StressTestSettings& settings);
        const StressTestSettings& GetStressTestSettings() const { return m_StressTest; }

        // Convenience presets for rapid iteration.
        // Preset 1: ~1.4k quads, Preset 2: ~10k quads, Preset 3: ~20k quads, Preset 4: ~50k quads
        void ApplyStressPreset(uint32_t presetIndex);

    private:
        void EnsureAssetsReady();

        Assets::TextureAsset::Ptr m_CheckerTexture;
        Assets::TextureAsset::Ptr m_SissyTexture;

        float m_TimeSeconds = 0.0f;
        float m_StatsLogAccumulatorSeconds = 0.0f;
        uint64_t m_FramesSinceLastStatsLog = 0;
        bool m_LoggedReadyOnce = false;

        StressTestSettings m_StressTest{};
    };
}

