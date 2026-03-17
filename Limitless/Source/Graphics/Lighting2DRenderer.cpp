#include "Graphics/Lighting2DRendererInternal.h"

namespace Limitless
{
    using namespace Lighting2DInternal;

    // Anonymous namespace helpers have been moved to Lighting2DRendererInternal.h.
    // Resource/shader/texture helpers live in Lighting2DRendererHelpers.cpp.
    // Shadow/light building lives in Lighting2DRendererShadows.cpp.
    // Render pass submission lives in Lighting2DRendererPasses.cpp.

    struct Lighting2DRenderer::Impl
    {
        Lighting2DRendererState State{};
    };

    Lighting2DRenderer* Lighting2DRenderer::s_Default = nullptr;

    Lighting2DRenderer::Lighting2DRenderer()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    Lighting2DRenderer::~Lighting2DRenderer()
    {
        if (s_Default == this)
            s_Default = nullptr;
    }

    Lighting2DRenderer& Lighting2DRenderer::Default()
    {
        if (!s_Default)
        {
            static Lighting2DRenderer s_DefaultInstance;
            s_Default = &s_DefaultInstance;
        }
        return *s_Default;
    }

    void Lighting2DRenderer::SetSettings(const Lighting2DSettings& settings)
    {
        g_State = &m_Impl->State;
        g_State->Settings = settings;
        g_State->Settings.ShadowQualityLevel = std::clamp(g_State->Settings.ShadowQualityLevel, 0, 2);
        g_State->Settings.MaxDirectionalLights = std::max(0, g_State->Settings.MaxDirectionalLights);
        g_State->Settings.MaxPointLights = std::max(0, g_State->Settings.MaxPointLights);
        g_State->Settings.MaxShadowSegments = std::max(1, g_State->Settings.MaxShadowSegments);
        g_State->Settings.MaxShadowSamplesPerLight = std::max(1, g_State->Settings.MaxShadowSamplesPerLight);
        g_State->Settings.AmbientIntensity = std::max(0.0f, g_State->Settings.AmbientIntensity);
        g_State->Settings.ShadowSoftnessScale = std::max(0.0f, g_State->Settings.ShadowSoftnessScale);
        g_State->Settings.DirectionalShadowBiasScale = std::max(0.0f, g_State->Settings.DirectionalShadowBiasScale);
        g_State->Settings.ShadowAlphaCutoff = std::clamp(g_State->Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
        g_State->Settings.ShadowSegmentSnapPixels = std::max(0.0f, g_State->Settings.ShadowSegmentSnapPixels);
        g_State->Settings.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, g_State->Settings.ShadowFreezeAngularVelocityDegreesPerSecond);
        g_State->Settings.ShadowFreezeFrameCount = std::max(1, g_State->Settings.ShadowFreezeFrameCount);
        if (!g_State->Settings.EnableHighAngularVelocityShadowFreeze)
            g_State->ShadowFreezeFramesRemaining = 0;
    }

    const Lighting2DSettings& Lighting2DRenderer::GetSettings() const
    {
        return m_Impl->State.Settings;
    }

    const Lighting2DDiagnostics& Lighting2DRenderer::GetDiagnostics() const
    {
        return m_Impl->State.Diagnostics;
    }

    bool Lighting2DRenderer::RenderToViewport(Scene& scene,
                          const Camera& camera,
                          const std::shared_ptr<Framebuffer>& targetFramebuffer,
                          uint32_t width,
                          uint32_t height,
                          const std::function<void()>& renderWorldAlbedoPass,
                          bool clearTarget)
    {
        g_State = &m_Impl->State;
        g_State->Diagnostics = {};

        if (!g_State->Settings.Enabled || width == 0 || height == 0)
            return false;

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
            return false;

        if (ShouldBypassDeferredLightingForDriver())
            return false;

        const auto buildStart = std::chrono::high_resolution_clock::now();
        if (!PrepareResources(width, height))
            return false;

        const float interpolationAlpha = ComputeInterpolationAlpha();
        const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
        const float pixelsPerUnit = EstimatePixelsPerWorldUnit(camera.GetViewMatrix(), viewProjection, width, height);
        const uint32_t cullingMask = GetEffectiveCameraCullingMask(camera);

        const glm::mat4 cameraViewMatrix = camera.GetViewMatrix();
        std::vector<NormalPassSpriteDraw> normalPassDraws = BuildNormalPassDrawList(scene, interpolationAlpha, pixelsPerUnit, cullingMask);
        uint32_t occluderCount = 0;
        const uint32_t maxShadowSegments = ClampSegmentsByQuality(g_State->Settings);
        float effectiveShadowSegmentSnapPixels = std::max(0.0f, g_State->Settings.ShadowSegmentSnapPixels);
        if (camera.GetUsage() == CameraUsage::Editor)
        {
            effectiveShadowSegmentSnapPixels = 0.0f;
        }
        const bool allowAngularVelocityShadowFreeze =
            g_State->Settings.EnableHighAngularVelocityShadowFreeze && camera.GetUsage() == CameraUsage::Gameplay;
        const glm::quat currentCameraRotation = ExtractCameraRotationFromViewMatrix(cameraViewMatrix);
        float cameraAngularVelocityDegreesPerSecond = 0.0f;
        if (allowAngularVelocityShadowFreeze && g_State->HasPreviousCameraRotation)
        {
            const float deltaTimeSeconds = std::max(Time::GetUnscaledDeltaTimeSeconds(), kEpsilon);
            cameraAngularVelocityDegreesPerSecond = ComputeAngularVelocityDegreesPerSecond(
                g_State->PreviousCameraRotation,
                currentCameraRotation,
                deltaTimeSeconds);
        }
        if (allowAngularVelocityShadowFreeze)
        {
            g_State->HasPreviousCameraRotation = true;
            g_State->PreviousCameraRotation = currentCameraRotation;
        }
        else
        {
            g_State->HasPreviousCameraRotation = false;
            g_State->ShadowFreezeFramesRemaining = 0;
        }

        const float freezeThreshold = g_State->Settings.ShadowFreezeAngularVelocityDegreesPerSecond;
        const int freezeFrameCount = g_State->Settings.ShadowFreezeFrameCount;

        if (allowAngularVelocityShadowFreeze &&
            cameraAngularVelocityDegreesPerSecond >= freezeThreshold)
        {
            const uint32_t requestedFreezeFrames = static_cast<uint32_t>(std::max(1, freezeFrameCount));
            g_State->ShadowFreezeFramesRemaining = std::max(g_State->ShadowFreezeFramesRemaining, requestedFreezeFrames);
        }

        std::vector<ShadowSegment> shadowSegments;
        const bool useFrozenShadowSegments = allowAngularVelocityShadowFreeze &&
            g_State->ShadowFreezeFramesRemaining > 0 &&
            !g_State->CachedShadowSegments.empty();
        if (useFrozenShadowSegments)
        {
            shadowSegments = g_State->CachedShadowSegments;
            occluderCount = g_State->CachedShadowOccluderCount;
        }
        else
        {
            shadowSegments = BuildShadowSegments(scene, interpolationAlpha, camera, viewProjection, width, height, maxShadowSegments, cullingMask, occluderCount, effectiveShadowSegmentSnapPixels);
            g_State->CachedShadowSegments = shadowSegments;
            g_State->CachedShadowOccluderCount = occluderCount;
        }
        if (allowAngularVelocityShadowFreeze && g_State->ShadowFreezeFramesRemaining > 0)
            --g_State->ShadowFreezeFramesRemaining;
        std::vector<ScreenDirectionalLight> directionalLights = BuildDirectionalLights(scene, interpolationAlpha, camera, viewProjection, width, height, pixelsPerUnit, cullingMask);
        std::vector<ScreenPointLight> pointLights = BuildPointLights(scene, interpolationAlpha, viewProjection, width, height, pixelsPerUnit, cullingMask);

        const auto buildEnd = std::chrono::high_resolution_clock::now();
        g_State->Diagnostics.CpuBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();
        g_State->Diagnostics.ShadowOccluderCount = occluderCount;
        g_State->Diagnostics.ShadowSegmentCount = static_cast<uint32_t>(shadowSegments.size());
        g_State->Diagnostics.DirectionalLightsRendered = static_cast<uint32_t>(directionalLights.size());
        g_State->Diagnostics.PointLightsRendered = static_cast<uint32_t>(pointLights.size());

        const auto submitStart = std::chrono::high_resolution_clock::now();

        const RenderPassDescriptor gBufferPass = BuildGBufferRenderPassDescriptor(g_State->GBufferFramebuffer, width, height);
        RenderPass::Begin(renderer, gBufferPass);
        SubmitSelectGBufferDrawBuffers();
        SubmitClearNormalAttachment();
        SubmitClearEntityIdAttachment();
        SubmitClearCasterMaskAttachment();
        SubmitClearCasterEntityIdAttachment();

        SubmitSelectAlbedoAttachmentOnly();
        if (renderWorldAlbedoPass)
            renderWorldAlbedoPass();

        SubmitNormalPassDraws(normalPassDraws, viewProjection);
        RenderPass::End(renderer, gBufferPass);

        const glm::vec3 ambient = glm::max(g_State->Settings.AmbientColor * g_State->Settings.AmbientIntensity, glm::vec3(0.0f));
        const RenderPassDescriptor lightPass = BuildLightAccumulationRenderPassDescriptor(g_State->LightFramebuffer, width, height, ambient);
        RenderPass::Begin(renderer, lightPass);

        const auto gBufferAlbedo = g_State->GBufferFramebuffer->GetColorAttachment(0);
        const auto gBufferNormal = g_State->GBufferFramebuffer->GetColorAttachment(1);
        const auto gBufferEntityId = g_State->GBufferFramebuffer->GetColorAttachment(2);
        const auto gBufferCasterMask = g_State->GBufferFramebuffer->GetColorAttachment(3);
        const auto gBufferCasterEntityId = g_State->GBufferFramebuffer->GetColorAttachment(4);
        auto directionalShader = ResolveShaderFromAsset(g_State->DirectionalLightShaderAsset, kDirectionalLightShaderKey);
        EnsureLightingPipeline(g_State->DirectionalLightPipeline,
                               g_State->DirectionalLightPipelineShader,
                               directionalShader,
                               "Lighting2D/DirectionalLight",
                               true,
                               BlendFactor::One,
                               BlendFactor::One,
                               false,
                               false);
        if (g_State->DirectionalLightPipeline)
        {
            renderer.SubmitCommand(std::make_unique<BindRenderPipelineCommand>(g_State->DirectionalLightPipeline));
        }
        else
        {
            renderer.SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::One, true));
        }
        for (const ScreenDirectionalLight& directionalLight : directionalLights)
        {
            SubmitDirectionalLightPass(gBufferAlbedo, gBufferNormal, gBufferEntityId, gBufferCasterMask, gBufferCasterEntityId, shadowSegments, directionalLight, width, height, effectiveShadowSegmentSnapPixels);
        }

        auto pointShader = ResolveShaderFromAsset(g_State->PointLightShaderAsset, kPointLightShaderKey);
        EnsureLightingPipeline(g_State->PointLightPipeline,
                               g_State->PointLightPipelineShader,
                               pointShader,
                               "Lighting2D/PointLight",
                               true,
                               BlendFactor::One,
                               BlendFactor::One,
                               false,
                               false);
        if (g_State->PointLightPipeline)
        {
            renderer.SubmitCommand(std::make_unique<BindRenderPipelineCommand>(g_State->PointLightPipeline));
        }
        for (const ScreenPointLight& pointLight : pointLights)
        {
            SubmitPointLightPass(gBufferAlbedo, gBufferNormal, gBufferEntityId, shadowSegments, pointLight, width, height, effectiveShadowSegmentSnapPixels);
        }

        renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::Zero, false));
        RenderPass::End(renderer, lightPass);

        const glm::vec4 fallbackClearColor = SceneRenderer::GetViewportClearColor();
        const RenderPassDescriptor compositePass = BuildCompositeRenderPassDescriptor(targetFramebuffer, width, height, fallbackClearColor, clearTarget);
        RenderPass::Begin(renderer, compositePass);

        auto compositeShader = ResolveShaderFromAsset(g_State->CompositeShaderAsset, kCompositeShaderKey);
        EnsureLightingPipeline(g_State->CompositePipeline,
                               g_State->CompositePipelineShader,
                               compositeShader,
                               "Lighting2D/Composite",
                               true,
                               BlendFactor::One,
                               BlendFactor::OneMinusSrcAlpha,
                               false,
                               false);
        if (g_State->CompositePipeline)
        {
            renderer.SubmitCommand(std::make_unique<BindRenderPipelineCommand>(g_State->CompositePipeline));
        }
        else
        {
            renderer.SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::OneMinusSrcAlpha, true));
        }
        SubmitCompositePass(gBufferAlbedo, g_State->LightFramebuffer->GetColorAttachment(0));
        RenderPass::End(renderer, compositePass);

        const auto submitEnd = std::chrono::high_resolution_clock::now();
        g_State->Diagnostics.CpuSubmitTimeMs = std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();
        g_State->Diagnostics.UsingLightingPath = true;
        return true;
    }
}
