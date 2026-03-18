#include "Graphics/Lighting2DRendererInternal.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"

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
        {
            g_State->ShadowFreezeFramesRemaining = 0;
            g_State->HasPreviousCameraRotation = false;
            g_State->HasPreviousCameraPosition = false;
            g_State->FramesSinceShadowSegmentBuild = 0;
            g_State->ShadowSurgeCadenceFramesRemaining = 0;
        }
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
                          const std::function<void(const Camera&, uint32_t, uint32_t)>& renderWorldAlbedoPass,
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

        const uint32_t cullingMask = GetEffectiveCameraCullingMask(camera);
        const float interpolationAlpha = ComputeInterpolationAlpha();

        const glm::mat4 baseViewProjection = camera.GetViewProjectionMatrix();
        const float basePixelsPerUnit = EstimatePixelsPerWorldUnit(camera.GetViewMatrix(), baseViewProjection, width, height);

        const Camera* lightingCamera = &camera;
        std::unique_ptr<PerspectiveCamera3D> overscanCamera;
        uint32_t lightingWidth = width;
        uint32_t lightingHeight = height;
        glm::vec2 compositeUvOffset(0.0f);
        glm::vec2 compositeUvScale(1.0f);
        if (camera.GetType() == CameraType::Perspective3D)
        {
            if (const auto* perspectiveCamera = dynamic_cast<const PerspectiveCamera3D*>(&camera))
            {
                constexpr float kGuardFraction = 0.06f;
                constexpr float kMaxGuardPerSide = 28.0f;
                const glm::mat4& viewMatrix = camera.GetViewMatrix();
                auto& registry = scene.GetRegistry();
                auto directionalView = registry.view<DirectionalLight2DComponent>();
                glm::vec2 guardBandPixels(0.0f);
                for (entt::entity entity : directionalView)
                {
                    if (!scene.IsEntityEnabledInHierarchy(entity))
                        continue;
                    if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                        continue;
                    auto& directional = directionalView.get<DirectionalLight2DComponent>(entity);
                    if (!directional.Enabled || !directional.CastShadows || directional.Intensity <= 0.0f)
                        continue;
                    glm::vec2 worldDir = directional.Direction;
                    if (directional.UseEntityRotation)
                    {
                        const glm::mat4 wt = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                        worldDir = glm::vec2(wt[0].x, wt[0].y);
                    }
                    const float wdLen = glm::length(worldDir);
                    if (wdLen <= kEpsilon) continue;
                    worldDir /= wdLen;
                    glm::vec4 vd = viewMatrix * glm::vec4(worldDir, 0.0f, 0.0f);
                    glm::vec2 screenDir(vd.x, vd.y);
                    const float sdLen = glm::length(screenDir);
                    if (sdLen <= kEpsilon) continue;
                    screenDir /= sdLen;
                    const float distPx = std::max(0.0f, directional.ShadowDistance) * basePixelsPerUnit;
                    guardBandPixels.x = std::max(guardBandPixels.x, std::min(std::abs(screenDir.x) * distPx * kGuardFraction + 4.0f, kMaxGuardPerSide));
                    guardBandPixels.y = std::max(guardBandPixels.y, std::min(std::abs(screenDir.y) * distPx * kGuardFraction + 4.0f, kMaxGuardPerSide));
                }
                if (guardBandPixels.x >= 1.0f || guardBandPixels.y >= 1.0f)
                {
                    const float scaleX = (static_cast<float>(width) + guardBandPixels.x * 2.0f) / static_cast<float>(width);
                    const float scaleY = (static_cast<float>(height) + guardBandPixels.y * 2.0f) / static_cast<float>(height);
                    constexpr uint32_t kQuantizeStep = 32;
                    lightingWidth = std::max(width, ((static_cast<uint32_t>(std::lround(static_cast<float>(width) * scaleX)) + kQuantizeStep - 1) / kQuantizeStep) * kQuantizeStep);
                    lightingHeight = std::max(height, ((static_cast<uint32_t>(std::lround(static_cast<float>(height) * scaleY)) + kQuantizeStep - 1) / kQuantizeStep) * kQuantizeStep);

                    PerspectiveCamera3D::Settings settings{};
                    const float originalFovRadians = glm::radians(perspectiveCamera->GetFieldOfViewYDegrees());
                    const float overscanFovRadians = 2.0f * std::atan(std::tan(originalFovRadians * 0.5f) * scaleY);
                    settings.FieldOfViewYDegrees = glm::degrees(std::min(overscanFovRadians, glm::radians(130.0f)));
                    settings.NearPlane = perspectiveCamera->GetNearPlane();
                    settings.FarPlane = perspectiveCamera->GetFarPlane();

                    overscanCamera = std::make_unique<PerspectiveCamera3D>(
                        CameraId{},
                        "Lighting2DOverscanCamera",
                        camera.GetUsage(),
                        lightingWidth,
                        lightingHeight,
                        settings);
                    overscanCamera->SetPosition(perspectiveCamera->GetPosition());
                    overscanCamera->SetYawPitchDegrees(perspectiveCamera->GetYawDegrees(), perspectiveCamera->GetPitchDegrees());
                    lightingCamera = overscanCamera.get();

                    compositeUvScale = glm::vec2(
                        static_cast<float>(width) / static_cast<float>(lightingWidth),
                        static_cast<float>(height) / static_cast<float>(lightingHeight));
                    compositeUvOffset = (glm::vec2(1.0f) - compositeUvScale) * 0.5f;
                }
            }
        }

        const auto buildStart = std::chrono::high_resolution_clock::now();
        if (!PrepareResources(lightingWidth, lightingHeight))
            return false;

        const float nativePixelCount = std::max(static_cast<float>(width) * static_cast<float>(height), 1.0f);
        const float lightingPixelCount = std::max(static_cast<float>(lightingWidth) * static_cast<float>(lightingHeight), 1.0f);
        const float overscanAreaScale = lightingPixelCount / nativePixelCount;

        const glm::mat4 viewProjection = lightingCamera->GetViewProjectionMatrix();
        const float pixelsPerUnit = EstimatePixelsPerWorldUnit(lightingCamera->GetViewMatrix(), viewProjection, lightingWidth, lightingHeight);

        const glm::mat4 cameraViewMatrix = lightingCamera->GetViewMatrix();
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
        const glm::mat4 cameraWorldMatrix = glm::inverse(cameraViewMatrix);
        const glm::vec3 currentCameraPosition = glm::vec3(cameraWorldMatrix[3]);
        float cameraAngularVelocityDegreesPerSecond = 0.0f;
        float cameraTranslationPixelsPerSecond = 0.0f;
        if (allowAngularVelocityShadowFreeze && g_State->HasPreviousCameraRotation)
        {
            const float deltaTimeSeconds = std::max(Time::GetUnscaledDeltaTimeSeconds(), kEpsilon);
            cameraAngularVelocityDegreesPerSecond = ComputeAngularVelocityDegreesPerSecond(
                g_State->PreviousCameraRotation,
                currentCameraRotation,
                deltaTimeSeconds);
            if (g_State->HasPreviousCameraPosition)
            {
                const float worldDistance = glm::length(currentCameraPosition - g_State->PreviousCameraPosition);
                cameraTranslationPixelsPerSecond = (worldDistance * pixelsPerUnit) / deltaTimeSeconds;
            }
        }
        if (allowAngularVelocityShadowFreeze)
        {
            g_State->HasPreviousCameraRotation = true;
            g_State->PreviousCameraRotation = currentCameraRotation;
            g_State->HasPreviousCameraPosition = true;
            g_State->PreviousCameraPosition = currentCameraPosition;
        }
        else
        {
            g_State->HasPreviousCameraRotation = false;
            g_State->HasPreviousCameraPosition = false;
            g_State->FramesSinceShadowSegmentBuild = 0;
            g_State->ShadowFreezeFramesRemaining = 0;
            g_State->ShadowSurgeCadenceFramesRemaining = 0;
        }

        const float freezeThreshold = g_State->Settings.ShadowFreezeAngularVelocityDegreesPerSecond;
        const float translationFreezeThresholdPixelsPerSecond = std::max(std::max(static_cast<float>(width), static_cast<float>(height)) * 0.65f, 900.0f);
        const float cadenceAngularVelocityThreshold = freezeThreshold * 0.2f;
        const float cadenceTranslationThresholdPixelsPerSecond = translationFreezeThresholdPixelsPerSecond * 0.3f;
        const int freezeFrameCount = g_State->Settings.ShadowFreezeFrameCount;
        const bool highMotionShadowFreezeTriggered = allowAngularVelocityShadowFreeze &&
            (cameraAngularVelocityDegreesPerSecond >= freezeThreshold ||
             cameraTranslationPixelsPerSecond >= translationFreezeThresholdPixelsPerSecond);

        if (highMotionShadowFreezeTriggered)
        {
            const uint32_t requestedFreezeFrames = static_cast<uint32_t>(std::max(1, freezeFrameCount));
            g_State->ShadowFreezeFramesRemaining = std::max(g_State->ShadowFreezeFramesRemaining, requestedFreezeFrames);
        }

        std::vector<ShadowSegment> shadowSegments;
        const bool motionAwareShadowCadenceActive = allowAngularVelocityShadowFreeze &&
            !highMotionShadowFreezeTriggered &&
            !g_State->CachedShadowSegments.empty() &&
            (cameraAngularVelocityDegreesPerSecond >= cadenceAngularVelocityThreshold ||
             cameraTranslationPixelsPerSecond >= cadenceTranslationThresholdPixelsPerSecond);
        uint32_t shadowRebuildIntervalFrames = 1;
        if (motionAwareShadowCadenceActive)
        {
            const float angularStress = freezeThreshold > kEpsilon
                ? (cameraAngularVelocityDegreesPerSecond / freezeThreshold)
                : 0.0f;
            const float translationStress = translationFreezeThresholdPixelsPerSecond > kEpsilon
                ? (cameraTranslationPixelsPerSecond / translationFreezeThresholdPixelsPerSecond)
                : 0.0f;
            const float motionStress = std::max(angularStress, translationStress);
            shadowRebuildIntervalFrames = motionStress >= 0.8f ? 3u : 2u;
        }
        const bool useFrozenShadowSegments = allowAngularVelocityShadowFreeze &&
            !g_State->CachedShadowSegments.empty() &&
            (g_State->ShadowFreezeFramesRemaining > 0 ||
             g_State->ShadowSurgeCadenceFramesRemaining > 0 ||
             (motionAwareShadowCadenceActive && (g_State->FramesSinceShadowSegmentBuild + 1u) < shadowRebuildIntervalFrames));
        if (useFrozenShadowSegments)
        {
            shadowSegments = g_State->CachedShadowSegments;
            occluderCount = g_State->CachedShadowOccluderCount;
            ++g_State->FramesSinceShadowSegmentBuild;
            if (g_State->ShadowSurgeCadenceFramesRemaining > 0)
                --g_State->ShadowSurgeCadenceFramesRemaining;
        }
        else
        {
            const uint32_t previousOccluderCount = g_State->CachedShadowOccluderCount;
            shadowSegments = BuildShadowSegments(scene, interpolationAlpha, *lightingCamera, viewProjection, lightingWidth, lightingHeight, maxShadowSegments, cullingMask, occluderCount, effectiveShadowSegmentSnapPixels);
            g_State->CachedShadowSegments = shadowSegments;
            g_State->CachedShadowOccluderCount = occluderCount;
            g_State->FramesSinceShadowSegmentBuild = 0;
            if (occluderCount > previousOccluderCount + 5u &&
                occluderCount > static_cast<uint32_t>(static_cast<float>(previousOccluderCount) * 1.5f))
            {
                g_State->ShadowSurgeCadenceFramesRemaining = 3;
            }
        }
        if (allowAngularVelocityShadowFreeze && g_State->ShadowFreezeFramesRemaining > 0)
            --g_State->ShadowFreezeFramesRemaining;
        std::vector<ScreenDirectionalLight> directionalLights = BuildDirectionalLights(scene, interpolationAlpha, *lightingCamera, viewProjection, lightingWidth, lightingHeight, pixelsPerUnit, cullingMask);
        std::vector<ScreenPointLight> pointLights = BuildPointLights(scene, interpolationAlpha, *lightingCamera, viewProjection, lightingWidth, lightingHeight, pixelsPerUnit, cullingMask);

        {
            float combinedScale = 1.0f;

            if (overscanAreaScale > 1.01f)
            {
                const float overscanExcess = overscanAreaScale - 1.0f;
                combinedScale *= std::clamp(1.0f - overscanExcess * 1.75f, 0.5f, 1.0f);
            }

            const float segmentLoad = maxShadowSegments > 0
                ? static_cast<float>(shadowSegments.size()) / static_cast<float>(maxShadowSegments)
                : 0.0f;
            if (segmentLoad > 0.25f)
                combinedScale *= std::clamp(1.0f - (segmentLoad - 0.25f) * 1.2f, 0.35f, 1.0f);

            if (combinedScale < 0.99f)
            {
                for (ScreenDirectionalLight& directionalLight : directionalLights)
                {
                    if (!directionalLight.CastShadows)
                        continue;

                    directionalLight.ShadowSamples = std::clamp(
                        static_cast<int>(std::lround(static_cast<float>(directionalLight.ShadowSamples) * combinedScale)),
                        2,
                        directionalLight.ShadowSamples);
                }

                for (ScreenPointLight& pointLight : pointLights)
                {
                    if (!pointLight.CastShadows)
                        continue;

                    pointLight.ShadowSamples = std::clamp(
                        static_cast<int>(std::lround(static_cast<float>(pointLight.ShadowSamples) * combinedScale)),
                        2,
                        pointLight.ShadowSamples);
                }
            }
        }

        const auto buildEnd = std::chrono::high_resolution_clock::now();
        g_State->Diagnostics.CpuBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();
        g_State->Diagnostics.ShadowOccluderCount = occluderCount;
        g_State->Diagnostics.ShadowSegmentCount = static_cast<uint32_t>(shadowSegments.size());
        g_State->Diagnostics.DirectionalLightsRendered = static_cast<uint32_t>(directionalLights.size());
        g_State->Diagnostics.PointLightsRendered = static_cast<uint32_t>(pointLights.size());

        const auto submitStart = std::chrono::high_resolution_clock::now();

        const RenderPassDescriptor gBufferPass = BuildGBufferRenderPassDescriptor(g_State->GBufferFramebuffer, lightingWidth, lightingHeight);
        RenderPass::Begin(renderer, gBufferPass);
        SubmitSelectGBufferDrawBuffers();
        SubmitClearNormalAttachment();
        SubmitClearEntityIdAttachment();
        SubmitClearCasterMaskAttachment();
        SubmitClearCasterEntityIdAttachment();

        SubmitSelectAlbedoAttachmentOnly();
        if (renderWorldAlbedoPass)
            renderWorldAlbedoPass(*lightingCamera, lightingWidth, lightingHeight);

        SubmitNormalPassDraws(normalPassDraws, viewProjection);
        RenderPass::End(renderer, gBufferPass);

        const glm::vec3 ambient = glm::max(g_State->Settings.AmbientColor * g_State->Settings.AmbientIntensity, glm::vec3(0.0f));
        const RenderPassDescriptor lightPass = BuildLightAccumulationRenderPassDescriptor(g_State->LightFramebuffer, lightingWidth, lightingHeight, ambient);
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
        const bool clampDirectionalShadowToViewport =
            lightingCamera->GetType() == CameraType::Perspective3D;
        for (const ScreenDirectionalLight& directionalLight : directionalLights)
        {
            SubmitDirectionalLightPass(gBufferAlbedo,
                                       gBufferNormal,
                                       gBufferEntityId,
                                       gBufferCasterMask,
                                       gBufferCasterEntityId,
                                       shadowSegments,
                                       directionalLight,
                                       lightingWidth,
                                       lightingHeight,
                                       effectiveShadowSegmentSnapPixels,
                                       clampDirectionalShadowToViewport);
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
            SubmitPointLightPass(gBufferAlbedo, gBufferNormal, gBufferEntityId, shadowSegments, pointLight, lightingWidth, lightingHeight, effectiveShadowSegmentSnapPixels);
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
        SubmitCompositePass(gBufferAlbedo, g_State->LightFramebuffer->GetColorAttachment(0), compositeUvOffset, compositeUvScale);
        RenderPass::End(renderer, compositePass);

        const auto submitEnd = std::chrono::high_resolution_clock::now();
        g_State->Diagnostics.CpuSubmitTimeMs = std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();
        g_State->Diagnostics.UsingLightingPath = true;
        return true;
    }
}
