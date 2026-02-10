#pragma once

#include "Limitless.h"

#include "Assets/TextureAssetImporter.h"

#include <memory>

namespace Limitless
{
    class Renderer2DDemo final
    {
    public:
        Renderer2DDemo() = default;
        ~Renderer2DDemo() = default;

        void Initialize(uint32_t viewportWidthPixels, uint32_t viewportHeightPixels);
        void Shutdown();

        void Update(float deltaTime);
        void Render(const Camera& camera) const;

    private:
        void EnsureAssetsReady();

        Assets::TextureAsset::Ptr m_CheckerTexture;
        Assets::TextureAsset::Ptr m_SissyTexture;

        float m_TimeSeconds = 0.0f;
        float m_StatsLogAccumulatorSeconds = 0.0f;
        bool m_LoggedReadyOnce = false;
    };
}

