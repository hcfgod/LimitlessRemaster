#include "Graphics/Lighting2DRendererInternal.h"

namespace Limitless::Lighting2DInternal
{
    void SubmitSelectDrawBuffers(std::vector<uint32_t> attachments)
    {
        Renderer::GetInstance().SubmitCommand(std::make_unique<SetDrawColorAttachmentsCommand>(std::move(attachments)));
    }

    void SubmitSelectGBufferDrawBuffers()
    {
        SubmitSelectDrawBuffers({ 0u, 1u, 2u, 3u, 4u });
    }

    void SubmitSelectAlbedoAttachmentOnly()
    {
        SubmitSelectDrawBuffers({ 0u });
    }

    void SubmitSelectNormalAndEntityAttachments()
    {
        SubmitSelectDrawBuffers({ 1u, 2u, 3u, 4u });
    }

    void SubmitClearNormalAttachment()
    {
        Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(1u, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)));
    }

    void SubmitClearEntityIdAttachment()
    {
        Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(2u, glm::vec4(0.0f)));
    }

    void SubmitClearCasterMaskAttachment()
    {
        Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(3u, glm::vec4(0.0f)));
    }

    void SubmitClearCasterEntityIdAttachment()
    {
        Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(4u, glm::vec4(0.0f)));
    }

    RenderPassDescriptor BuildGBufferRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                          uint32_t width,
                                                          uint32_t height)
    {
        RenderPassDescriptor descriptor{};
        descriptor.DebugName = "Lighting2D/GBuffer";
        descriptor.TargetFramebuffer = framebuffer;
        descriptor.Viewport = RenderViewport{ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
        descriptor.ColorAttachments = {
            RenderPassColorAttachmentDescriptor{ RenderLoadAction::Clear, RenderStoreAction::Store, glm::vec4(0.0f) },
            RenderPassColorAttachmentDescriptor{ RenderLoadAction::Clear, RenderStoreAction::Store, glm::vec4(0.0f) },
            RenderPassColorAttachmentDescriptor{ RenderLoadAction::Clear, RenderStoreAction::Store, glm::vec4(0.0f) },
            RenderPassColorAttachmentDescriptor{ RenderLoadAction::Clear, RenderStoreAction::Store, glm::vec4(0.0f) },
            RenderPassColorAttachmentDescriptor{ RenderLoadAction::Clear, RenderStoreAction::Store, glm::vec4(0.0f) }
        };
        descriptor.DepthStencilAttachment = RenderPassDepthStencilAttachmentDescriptor{
            RenderLoadAction::Clear,
            RenderStoreAction::Store,
            RenderLoadAction::DontCare,
            RenderStoreAction::DontCare,
            1.0f,
            0u
        };
        return descriptor;
    }

    RenderPassDescriptor BuildLightAccumulationRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                                    uint32_t width,
                                                                    uint32_t height,
                                                                    const glm::vec3& ambientColor)
    {
        RenderPassDescriptor descriptor{};
        descriptor.DebugName = "Lighting2D/LightAccumulation";
        descriptor.TargetFramebuffer = framebuffer;
        descriptor.Viewport = RenderViewport{ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
        descriptor.ColorAttachments = {
            RenderPassColorAttachmentDescriptor{
                RenderLoadAction::Clear,
                RenderStoreAction::Store,
                glm::vec4(ambientColor, 1.0f)
            }
        };
        return descriptor;
    }

    RenderPassDescriptor BuildCompositeRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                            uint32_t width,
                                                            uint32_t height,
                                                            const glm::vec4& clearColor,
                                                            bool clearTarget)
    {
        RenderPassDescriptor descriptor{};
        descriptor.DebugName = "Lighting2D/Composite";
        descriptor.TargetFramebuffer = framebuffer;
        descriptor.Viewport = RenderViewport{ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
        descriptor.ColorAttachments = {
            RenderPassColorAttachmentDescriptor{ clearTarget ? RenderLoadAction::Clear : RenderLoadAction::Load, RenderStoreAction::Store, clearColor }
        };
        descriptor.DepthStencilAttachment = RenderPassDepthStencilAttachmentDescriptor{
            RenderLoadAction::Clear,
            RenderStoreAction::Store,
            RenderLoadAction::DontCare,
            RenderStoreAction::DontCare,
            1.0f,
            0u
        };
        return descriptor;
    }

    void SubmitNormalPassDraws(const std::vector<NormalPassSpriteDraw>& drawList, const glm::mat4& viewProjection)
    {
        if (drawList.empty())
            return;

        auto shader = ResolveShaderFromAsset(g_State->GBufferNormalShaderAsset, kGBufferNormalShaderKey);
        if (!shader || !g_State->UnitQuadVertexArray)
            return;

        EnsureLightingPipeline(g_State->GBufferNormalPipeline,
                               g_State->GBufferNormalPipelineShader,
                               shader,
                               "Lighting2D/GBufferNormal",
                               false,
                               BlendFactor::One,
                               BlendFactor::Zero,
                               false,
                               false);
        if (g_State->GBufferNormalPipeline)
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<BindRenderPipelineCommand>(g_State->GBufferNormalPipeline));
        }
        else
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            Renderer::GetInstance().SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::Zero, false));
            Renderer::GetInstance().SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
        }
        SubmitSelectNormalAndEntityAttachments();
        const float shadowAlphaCutoff = std::clamp(g_State->Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
        const float framebufferWidth = static_cast<float>(std::max<uint32_t>(g_State->FramebufferWidth, 1u));
        const float framebufferHeight = static_cast<float>(std::max<uint32_t>(g_State->FramebufferHeight, 1u));
        const float casterHeightEncodeMaxPixels = std::max(std::sqrt(framebufferWidth * framebufferWidth + framebufferHeight * framebufferHeight), 1.0f);

        for (const NormalPassSpriteDraw& draw : drawList)
        {
            auto shaderRef = shader;
            auto vertexArrayRef = g_State->UnitQuadVertexArray;
            auto albedoRef = draw.AlbedoTexture ? draw.AlbedoTexture : g_State->WhiteTexture;
            auto normalRef = draw.NormalTexture ? draw.NormalTexture : g_State->FlatNormalTexture;
            const glm::mat4 model = draw.Model;
            const glm::vec4 color = draw.Color;
            const glm::vec2 uvMin = draw.UvMin;
            const glm::vec2 uvMax = draw.UvMax;
            const float normalStrength = draw.NormalStrength;
            const int receiveShadows = draw.ReceiveShadows ? 1 : 0;
            const float casterHeightPixels = draw.CasterHeightPixels;
            const glm::vec4 casterEntityId = draw.CasterEntityId;
            RenderBindingSet bindings{};
            bindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
            bindings.Textures.push_back(RenderTextureBinding{ "u_NormalTexture", normalRef, 1u });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_ViewProjection", viewProjection });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_Model", model });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_Color", color });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_UvMin", uvMin });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_UvMax", uvMax });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_NormalStrength", normalStrength });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_ReceiveShadows", static_cast<int32_t>(receiveShadows) });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_CasterHeightPixels", casterHeightPixels });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_CasterHeightEncodeMaxPixels", casterHeightEncodeMaxPixels });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_CasterEntityId", casterEntityId });
            bindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowAlphaCutoff", shadowAlphaCutoff });
            Renderer::GetInstance().SubmitCommand(std::make_unique<ApplyRenderBindingsCommand>(shaderRef, vertexArrayRef, std::move(bindings)));
            Renderer::GetInstance().SubmitCommand(std::make_unique<DrawArraysCommand>(DrawMode::TriangleStrip, 0, 4));
        }

        SubmitSelectGBufferDrawBuffers();
    }

    void SubmitDirectionalLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                                    const std::shared_ptr<Texture2D>& normalTexture,
                                    const std::shared_ptr<Texture2D>& entityIdTexture,
                                    const std::shared_ptr<Texture2D>& casterMaskTexture,
                                    const std::shared_ptr<Texture2D>& casterEntityIdTexture,
                                    const std::vector<ShadowSegment>& shadowSegments,
                                    const ScreenDirectionalLight& light,
                                    uint32_t width,
                                    uint32_t height,
                                    float shadowSegmentSnapPixels,
                                    bool clampShadowToViewport)
    {
        auto shader = ResolveShaderFromAsset(g_State->DirectionalLightShaderAsset, kDirectionalLightShaderKey);
        if (!shader || !g_State->UnitQuadVertexArray)
            return;

        auto shaderRef = shader;
        auto vertexArrayRef = g_State->UnitQuadVertexArray;
        auto albedoRef = albedoTexture;
        auto normalRef = normalTexture;
        auto entityIdRef = entityIdTexture;
        auto casterMaskRef = casterMaskTexture;
        auto casterEntityIdRef = casterEntityIdTexture;
        const glm::vec3 lightColor = light.Color;
        const float intensity = light.Intensity;
        const glm::vec2 shadowDirection = light.ShadowDirection;
        const glm::vec2 shadingDirection = light.ShadingDirection;
        const int useShadows = light.CastShadows ? 1 : 0;
        const float shadowStrength = light.ShadowStrength;
        const float shadowSoftness = light.ShadowSoftnessPixels;
        const int shadowSamples = light.ShadowSamples;
        const float shadowDistance = light.ShadowDistancePixels;
        const float shadowBias = light.ShadowBiasPixels;
        const float shadowAlphaCutoff = std::clamp(g_State->Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
        const float shadowSegmentSnapPixelsClamped = std::max(0.0f, shadowSegmentSnapPixels);
        const int clampShadowToViewportInt = clampShadowToViewport ? 1 : 0;
        const float viewportWidth = static_cast<float>(std::max<uint32_t>(width, 1u));
        const float viewportHeight = static_cast<float>(std::max<uint32_t>(height, 1u));
        const float casterHeightEncodeMaxPixels = std::max(std::sqrt(viewportWidth * viewportWidth + viewportHeight * viewportHeight), 1.0f);
        // Use all shadow segments without screen-space facing filter.
        // The ray-segment intersection in the shader is geometrically
        // correct regardless of edge orientation, so the filter was purely
        // a performance optimization.  Under perspective camera rotation
        // the screen-space edge normals shift rapidly, causing the filter
        // to pop edges in/out and produce shadow flicker.
        const size_t segmentCountClamped = std::min<size_t>(shadowSegments.size(), kShaderShadowSegmentCap);
        std::vector<glm::vec4> segmentEndpoints;
        std::vector<glm::vec4> segmentCasterIds;
        std::vector<int32_t> segmentFlags;
        segmentEndpoints.reserve(segmentCountClamped);
        segmentCasterIds.reserve(segmentCountClamped);
        segmentFlags.reserve(segmentCountClamped);
        for (size_t i = 0; i < segmentCountClamped; ++i)
        {
            segmentEndpoints.push_back(shadowSegments[i].Endpoints);
            segmentCasterIds.push_back(shadowSegments[i].CasterEntityId);
            segmentFlags.push_back(shadowSegments[i].Flags);
        }

        RenderBindingSet directionalBindings{};
        directionalBindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
        directionalBindings.Textures.push_back(RenderTextureBinding{ "u_NormalTexture", normalRef, 1u });
        directionalBindings.Textures.push_back(RenderTextureBinding{ "u_EntityIdTexture", entityIdRef, 2u });
        directionalBindings.Textures.push_back(RenderTextureBinding{ "u_CasterMaskTexture", casterMaskRef, 3u });
        directionalBindings.Textures.push_back(RenderTextureBinding{ "u_CasterEntityIdTexture", casterEntityIdRef, 4u });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ViewportSize", glm::vec2(static_cast<float>(width), static_cast<float>(height)) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_LightColor", lightColor });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_LightIntensity", intensity });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_LightDirection", shadowDirection });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadingLightDirection", shadingDirection });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_UseShadows", static_cast<int32_t>(useShadows) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowStrength", shadowStrength });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSoftness", shadowSoftness });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSamples", static_cast<int32_t>(shadowSamples) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowDistance", shadowDistance });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowBias", shadowBias });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowAlphaCutoff", shadowAlphaCutoff });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_CasterHeightEncodeMaxPixels", casterHeightEncodeMaxPixels });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentSnapPixels", shadowSegmentSnapPixelsClamped });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ClampShadowToViewport", static_cast<int32_t>(clampShadowToViewportInt) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCount", static_cast<int32_t>(segmentEndpoints.size()) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegments", std::move(segmentEndpoints) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCasterIds", std::move(segmentCasterIds) });
        directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentFlags", std::move(segmentFlags) });
        Renderer::GetInstance().SubmitCommand(std::make_unique<ApplyRenderBindingsCommand>(shaderRef, vertexArrayRef, std::move(directionalBindings)));
        Renderer::GetInstance().SubmitCommand(std::make_unique<DrawArraysCommand>(DrawMode::TriangleStrip, 0, 4));
    }

    void SubmitPointLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                              const std::shared_ptr<Texture2D>& normalTexture,
                              const std::shared_ptr<Texture2D>& entityIdTexture,
                              const std::vector<ShadowSegment>& shadowSegments,
                              const ScreenPointLight& light,
                              uint32_t width,
                              uint32_t height,
                              float shadowSegmentSnapPixels)
    {
        auto shader = ResolveShaderFromAsset(g_State->PointLightShaderAsset, kPointLightShaderKey);
        if (!shader || !g_State->UnitQuadVertexArray)
            return;

        auto shaderRef = shader;
        auto vertexArrayRef = g_State->UnitQuadVertexArray;
        auto albedoRef = albedoTexture;
        auto normalRef = normalTexture;
        auto entityIdRef = entityIdTexture;
        const glm::vec3 lightColor = light.Color;
        const float intensity = light.Intensity;
        const glm::vec2 lightPosition = light.Position;
        const float lightRadius = light.RadiusPixels;
        const float lightFalloff = light.Falloff;
        const int useShadows = light.CastShadows ? 1 : 0;
        const float shadowStrength = light.ShadowStrength;
        const float shadowSoftness = light.ShadowSoftnessPixels;
        const int shadowSamples = light.ShadowSamples;
        const float shadowBias = light.ShadowBiasPixels;
        const float shadowAlphaCutoff = std::clamp(g_State->Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
        const float shadowSegmentSnapPixelsClamped = std::max(0.0f, shadowSegmentSnapPixels);
        const size_t segmentCountClamped = std::min<size_t>(shadowSegments.size(), kShaderShadowSegmentCap);
        std::vector<glm::vec4> segmentEndpoints;
        std::vector<glm::vec4> segmentCasterIds;
        segmentEndpoints.reserve(segmentCountClamped);
        segmentCasterIds.reserve(segmentCountClamped);
        for (size_t i = 0; i < segmentCountClamped; ++i)
        {
            segmentEndpoints.push_back(shadowSegments[i].Endpoints);
            segmentCasterIds.push_back(shadowSegments[i].CasterEntityId);
        }

        RenderBindingSet pointBindings{};
        pointBindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
        pointBindings.Textures.push_back(RenderTextureBinding{ "u_NormalTexture", normalRef, 1u });
        pointBindings.Textures.push_back(RenderTextureBinding{ "u_EntityIdTexture", entityIdRef, 2u });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ViewportSize", glm::vec2(static_cast<float>(width), static_cast<float>(height)) });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_LightColor", lightColor });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_LightIntensity", intensity });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_LightPosition", lightPosition });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_LightRadius", lightRadius });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_LightFalloff", lightFalloff });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_UseShadows", static_cast<int32_t>(useShadows) });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowStrength", shadowStrength });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSoftness", shadowSoftness });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSamples", static_cast<int32_t>(shadowSamples) });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowBias", shadowBias });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowAlphaCutoff", shadowAlphaCutoff });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentSnapPixels", shadowSegmentSnapPixelsClamped });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCount", static_cast<int32_t>(segmentEndpoints.size()) });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegments", std::move(segmentEndpoints) });
        pointBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCasterIds", std::move(segmentCasterIds) });
        Renderer::GetInstance().SubmitCommand(std::make_unique<ApplyRenderBindingsCommand>(shaderRef, vertexArrayRef, std::move(pointBindings)));
        Renderer::GetInstance().SubmitCommand(std::make_unique<DrawArraysCommand>(DrawMode::TriangleStrip, 0, 4));
    }

    void SubmitCompositePass(const std::shared_ptr<Texture2D>& albedoTexture,
                             const std::shared_ptr<Texture2D>& lightTexture)
    {
        auto shader = ResolveShaderFromAsset(g_State->CompositeShaderAsset, kCompositeShaderKey);
        if (!shader || !g_State->UnitQuadVertexArray || !albedoTexture || !lightTexture)
            return;

        auto shaderRef = shader;
        auto vertexArrayRef = g_State->UnitQuadVertexArray;
        auto albedoRef = albedoTexture;
        auto lightRef = lightTexture;
        RenderBindingSet compositeBindings{};
        compositeBindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
        compositeBindings.Textures.push_back(RenderTextureBinding{ "u_LightTexture", lightRef, 1u });
        Renderer::GetInstance().SubmitCommand(std::make_unique<ApplyRenderBindingsCommand>(shaderRef, vertexArrayRef, std::move(compositeBindings)));
        Renderer::GetInstance().SubmitCommand(std::make_unique<DrawArraysCommand>(DrawMode::TriangleStrip, 0, 4));
    }
}
