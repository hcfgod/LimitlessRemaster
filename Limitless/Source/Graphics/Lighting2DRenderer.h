#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <glm/glm.hpp>

namespace Limitless
{
    class Camera;
    class Framebuffer;
    class Scene;

    struct Lighting2DSettings
    {
        bool Enabled = true;
        bool EnableNormalMaps = true;
        bool EnableShadows = true;

        glm::vec3 AmbientColor = glm::vec3(0.12f, 0.12f, 0.14f);
        float AmbientIntensity = 0.6f;

        int ShadowQualityLevel = 1; // 0 = Low, 1 = Medium, 2 = High
        int MaxDirectionalLights = 4;
        int MaxPointLights = 32;
        int MaxShadowSegments = 128;

        float ShadowSoftnessScale = 1.0f;
        float DirectionalShadowBiasScale = 1.0f;
        float ShadowAlphaCutoff = 0.5f;
        float ShadowSegmentSnapPixels = 0.75f;
        bool EnableHighAngularVelocityShadowFreeze = true;
        float ShadowFreezeAngularVelocityDegreesPerSecond = 180.0f;
        int ShadowFreezeFrameCount = 2;
        int MaxShadowSamplesPerLight = 12;
    };

    struct Lighting2DDiagnostics
    {
        bool UsingLightingPath = false;
        uint32_t DirectionalLightsRendered = 0;
        uint32_t PointLightsRendered = 0;
        uint32_t ShadowOccluderCount = 0;
        uint32_t ShadowSegmentCount = 0;
        float CpuBuildTimeMs = 0.0f;
        float CpuSubmitTimeMs = 0.0f;
    };

    // -------------------------------------------------------------------------
    // Lighting2DRenderer
    // Instantiable deferred-style 2D lighting renderer.
    //
    // Multiple instances can coexist for split-screen / multi-viewport editors.
    // Lighting2DRenderer::Default() returns the global default instance.
    // -------------------------------------------------------------------------
    class Lighting2DRenderer final
    {
    public:
        Lighting2DRenderer();
        ~Lighting2DRenderer();

        Lighting2DRenderer(const Lighting2DRenderer&) = delete;
        Lighting2DRenderer& operator=(const Lighting2DRenderer&) = delete;

        void SetSettings(const Lighting2DSettings& settings);
        const Lighting2DSettings& GetSettings() const;

        const Lighting2DDiagnostics& GetDiagnostics() const;

        bool RenderToViewport(Scene& scene,
                              const Camera& camera,
                              const std::shared_ptr<Framebuffer>& targetFramebuffer,
                              uint32_t width,
                              uint32_t height,
                              const std::function<void()>& renderWorldAlbedoPass);

        static Lighting2DRenderer& Default();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        static Lighting2DRenderer* s_Default;
    };
}
