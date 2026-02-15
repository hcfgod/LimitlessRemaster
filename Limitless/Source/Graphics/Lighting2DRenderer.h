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

    namespace Lighting2DRenderer
    {
        void SetSettings(const Lighting2DSettings& settings);
        const Lighting2DSettings& GetSettings();

        const Lighting2DDiagnostics& GetDiagnostics();

        // Draws world-space 2D content with deferred-style lighting into target framebuffer.
        // Returns true when lighting path executed; false lets caller use fallback path.
        bool RenderToViewport(Scene& scene,
                              const Camera& camera,
                              const std::shared_ptr<Framebuffer>& targetFramebuffer,
                              uint32_t width,
                              uint32_t height,
                              const std::function<void()>& renderWorldAlbedoPass);
    }
}

