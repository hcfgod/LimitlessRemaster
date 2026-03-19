#pragma once

// Internal header shared across the Lighting2DRenderer split translation units.
// NOT part of the public engine API.

#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/SpriteAlphaHull.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Debug/Log.h"
#include "Core/StringUtils.h"
#include "Core/Time.h"
#include "Graphics/Buffer.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/RenderPass.h"
#include "Graphics/RenderPipeline.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include "Graphics/VertexArray.h"
#include "Platform/Platform.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/SceneRenderCulling.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Limitless::Lighting2DInternal
{
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    constexpr const char* kGBufferNormalShaderKey = "Assets/Shaders/Lighting2D_GBufferNormalPass.glsl";
    constexpr const char* kDirectionalLightShaderKey = "Assets/Shaders/Lighting2D_DirectionalLight.glsl";
    constexpr const char* kPointLightShaderKey = "Assets/Shaders/Lighting2D_PointLight.glsl";
    constexpr const char* kCompositeShaderKey = "Assets/Shaders/Lighting2D_Composite.glsl";

    constexpr uint32_t kShaderShadowSegmentCap = 128;
    constexpr uint32_t kPhysicsCircleSegmentApproximation = 12;
    constexpr float kEpsilon = 0.0001f;
    constexpr int32_t kShadowSegmentFlagOffscreenOnly = 1 << 0;

    // -------------------------------------------------------------------------
    // Structs
    // -------------------------------------------------------------------------

    struct NormalPassSpriteDraw
    {
        glm::mat4 Model = glm::mat4(1.0f);
        glm::vec4 Color = glm::vec4(1.0f);
        glm::vec2 UvMin = glm::vec2(0.0f, 0.0f);
        glm::vec2 UvMax = glm::vec2(1.0f, 1.0f);
        float NormalStrength = 1.0f;
        bool ReceiveShadows = true;
        float CasterHeightPixels = 0.0f;
        glm::vec4 CasterEntityId = glm::vec4(0.0f);
        std::shared_ptr<Texture2D> AlbedoTexture;
        std::shared_ptr<Texture2D> NormalTexture;
    };

    struct ShadowSegment
    {
        glm::vec4 Endpoints = glm::vec4(0.0f);
        glm::vec4 CasterEntityId = glm::vec4(0.0f);
        int32_t Flags = 0;
    };

    struct ScreenDirectionalLight
    {
        glm::vec3 Color = glm::vec3(1.0f);
        float Intensity = 1.0f;
        // Screen-space direction for shadow ray marching.
        glm::vec2 ShadowDirection = glm::vec2(0.0f, -1.0f);
        // World-space XY direction for diffuse shading.
        glm::vec2 ShadingDirection = glm::vec2(0.0f, -1.0f);
        bool CastShadows = true;
        float ShadowStrength = 1.0f;
        float ShadowSoftnessPixels = 0.0f;
        int ShadowSamples = 1;
        float ShadowDistancePixels = 500.0f;
        float ShadowBiasPixels = 0.0f;
    };

    struct ScreenPointLight
    {
        glm::vec3 Color = glm::vec3(1.0f);
        float Intensity = 1.0f;
        glm::vec2 Position = glm::vec2(0.0f);
        float RadiusPixels = 64.0f;
        float Falloff = 2.0f;
        bool CastShadows = true;
        float ShadowStrength = 1.0f;
        float ShadowSoftnessPixels = 0.0f;
        int ShadowSamples = 1;
        float ShadowBiasPixels = 0.0f;
    };

    struct ShadowCacheState
    {
        std::vector<ShadowSegment> CachedShadowSegments;
        uint32_t CachedShadowOccluderCount = 0;
        bool HasPreviousCameraRotation = false;
        glm::quat PreviousCameraRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        bool HasPreviousCameraPosition = false;
        glm::vec3 PreviousCameraPosition = glm::vec3(0.0f);
        uint32_t FramesSinceShadowSegmentBuild = 0;
        uint32_t ShadowFreezeFramesRemaining = 0;
        uint32_t ShadowSurgeCadenceFramesRemaining = 0;
    };

    struct ShadowCacheEntry
    {
        ShadowCacheState State{};
        uint64_t LastUsedTick = 0;
    };

    struct Lighting2DRendererState
    {
        Lighting2DSettings Settings{};
        Lighting2DDiagnostics Diagnostics{};

        std::shared_ptr<Framebuffer> GBufferFramebuffer;
        std::shared_ptr<Framebuffer> LightFramebuffer;
        uint32_t FramebufferWidth = 0;
        uint32_t FramebufferHeight = 0;
        uint64_t FramebufferUseTick = 0;

        struct FramebufferSet
        {
            std::shared_ptr<Framebuffer> GBuffer;
            std::shared_ptr<Framebuffer> Light;
            uint64_t LastUsedTick = 0;
        };
        std::unordered_map<uint64_t, FramebufferSet> FramebufferSets;

        std::shared_ptr<VertexArray> UnitQuadVertexArray;
        std::shared_ptr<VertexBuffer> UnitQuadVertexBuffer;

        std::shared_ptr<Texture2D> WhiteTexture;
        std::shared_ptr<Texture2D> FlatNormalTexture;

        Assets::ShaderAsset::Ptr GBufferNormalShaderAsset;
        Assets::ShaderAsset::Ptr DirectionalLightShaderAsset;
        Assets::ShaderAsset::Ptr PointLightShaderAsset;
        Assets::ShaderAsset::Ptr CompositeShaderAsset;
        std::shared_ptr<RenderPipeline> GBufferNormalPipeline;
        std::shared_ptr<Shader> GBufferNormalPipelineShader;
        std::shared_ptr<RenderPipeline> DirectionalLightPipeline;
        std::shared_ptr<Shader> DirectionalLightPipelineShader;
        std::shared_ptr<RenderPipeline> PointLightPipeline;
        std::shared_ptr<Shader> PointLightPipelineShader;
        std::shared_ptr<RenderPipeline> CompositePipeline;
        std::shared_ptr<Shader> CompositePipelineShader;

        uint64_t ShadowCacheUseTick = 0;
        std::unordered_map<const Camera*, ShadowCacheEntry> ShadowCacheEntries;
    };

    // C++17 inline variable — single definition shared across all TUs.
    inline Lighting2DRendererState* g_State = nullptr;

    // -------------------------------------------------------------------------
    // Small inline utility helpers
    // -------------------------------------------------------------------------

    inline uint64_t MakeFramebufferSetKey(uint32_t width, uint32_t height)
    {
        return (static_cast<uint64_t>(width) << 32) | static_cast<uint64_t>(height);
    }

    inline int ClampShadowSamplesByQuality(const Lighting2DSettings& settings, int requestedSamples)
    {
        const int qualityCappedSamples = [&]() -> int {
            switch (settings.ShadowQualityLevel)
            {
            case 0: return 4;
            case 1: return 8;
            case 2:
            default: return std::max(2, settings.MaxShadowSamplesPerLight);
            }
        }();
        return std::clamp(std::max(requestedSamples, 2), 2, std::max(2, qualityCappedSamples));
    }

    inline uint32_t ClampSegmentsByQuality(const Lighting2DSettings& settings)
    {
        const uint32_t userMax = static_cast<uint32_t>(std::max(1, settings.MaxShadowSegments));
        const uint32_t qualityMax = [&]() -> uint32_t {
            switch (settings.ShadowQualityLevel)
            {
            case 0: return 48;
            case 1: return 96;
            case 2:
            default: return userMax;
            }
        }();
        return std::min<uint32_t>(kShaderShadowSegmentCap, std::min(userMax, qualityMax));
    }

    inline uint32_t ClampPointLightsByQuality(const Lighting2DSettings& settings)
    {
        const uint32_t userMax = static_cast<uint32_t>(std::max(0, settings.MaxPointLights));
        const uint32_t qualityMax = [&]() -> uint32_t {
            switch (settings.ShadowQualityLevel)
            {
            case 0: return 8;
            case 1: return 16;
            case 2:
            default: return userMax;
            }
        }();
        return std::min(userMax, qualityMax);
    }

    inline uint32_t ClampDirectionalLightsByQuality(const Lighting2DSettings& settings)
    {
        const uint32_t userMax = static_cast<uint32_t>(std::max(0, settings.MaxDirectionalLights));
        return std::min<uint32_t>(userMax, 8);
    }

    inline glm::vec4 EncodeEntityIdToUnitVec4(entt::entity entity)
    {
        if (entity == entt::null)
            return glm::vec4(0.0f);

        const uint32_t raw = static_cast<uint32_t>(entt::to_integral(entity));
        const uint32_t id32 = raw + 1u;
        const float r = static_cast<float>(id32 & 0xFFu) / 255.0f;
        const float g = static_cast<float>((id32 >> 8) & 0xFFu) / 255.0f;
        const float b = static_cast<float>((id32 >> 16) & 0xFFu) / 255.0f;
        const float a = static_cast<float>((id32 >> 24) & 0xFFu) / 255.0f;
        return glm::vec4(r, g, b, a);
    }

    // -------------------------------------------------------------------------
    // Helpers declared here, defined in Lighting2DRendererHelpers.cpp
    // -------------------------------------------------------------------------

    bool ShouldBypassDeferredLightingForDriver();

    bool ProjectWorldToScreen(const glm::mat4& viewProjection,
                              const glm::vec3& worldPosition,
                              uint32_t width,
                              uint32_t height,
                              glm::vec2& outScreenPosition);

    glm::vec2 ProjectWorldToScreenClamped(const glm::mat4& viewProjection,
                                          const glm::vec3& worldPosition,
                                          uint32_t width,
                                          uint32_t height);

    float EstimatePixelsPerWorldUnit(const glm::mat4& viewMatrix,
                                     const glm::mat4& viewProjection,
                                     uint32_t width,
                                     uint32_t height);

    float ComputeInterpolationAlpha();

    glm::quat ExtractCameraRotationFromViewMatrix(const glm::mat4& viewMatrix);

    float ComputeAngularVelocityDegreesPerSecond(const glm::quat& previousRotation,
                                                  const glm::quat& currentRotation,
                                                  float deltaTimeSeconds);

    bool IsEntityInCanvasUiHierarchy(const entt::registry& registry, entt::entity entity);
    bool IsEntityInLightHierarchy(const entt::registry& registry, entt::entity entity);
    uint8_t GetEntityLayerForCameraCulling(const entt::registry& registry, entt::entity entity);
    uint32_t GetEffectiveCameraCullingMask(const Camera& camera);
    bool IsEntityVisibleToCameraCullingMask(const entt::registry& registry, entt::entity entity, uint32_t cullingMask);

    void EnsureShaderAssetLoaded(Assets::ShaderAsset::Ptr& shaderAsset, const char* shaderKey);
    std::shared_ptr<Shader> ResolveShaderFromAsset(Assets::ShaderAsset::Ptr& shaderAsset, const char* shaderKey);
    void EnsureFallbackTextures();
    void EnsureQuadGeometryCreated();
    bool EnsureFramebuffers(uint32_t width, uint32_t height);
    void RefreshSpriteMaterialCache(MaterialComponent& material);
    void RefreshSpriteTextureCache(SpriteComponent& sprite);
    bool PrepareResources(uint32_t width, uint32_t height);

    void EnsureLightingPipeline(std::shared_ptr<RenderPipeline>& pipeline,
                                std::shared_ptr<Shader>& cachedShader,
                                const std::shared_ptr<Shader>& shader,
                                const char* debugName,
                                bool blendEnabled,
                                BlendFactor srcBlend,
                                BlendFactor dstBlend,
                                bool depthTestEnabled,
                                bool depthWriteEnabled);

    // -------------------------------------------------------------------------
    // Shadow / light building — defined in Lighting2DRendererShadows.cpp
    // -------------------------------------------------------------------------

    std::vector<entt::entity> BuildSortedSpriteRenderList(Scene& scene, float interpolationAlpha, uint32_t cullingMask);
    std::vector<NormalPassSpriteDraw> BuildNormalPassDrawList(Scene& scene, float interpolationAlpha, float pixelsPerUnit, uint32_t cullingMask);

    void BuildPhysicsOccluderPolygon(const entt::registry& registry, entt::entity entity, std::vector<glm::vec2>& outPoints);
    std::vector<glm::vec2> ResolveOccluderLocalPolygon(const entt::registry& registry, entt::entity entity, const ShadowOccluder2DComponent& occluder);

    std::vector<ShadowSegment> BuildShadowSegments(Scene& scene,
                                                    float interpolationAlpha,
                                                    const Camera& camera,
                                                    const glm::mat4& viewProjection,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    uint32_t maxSegments,
                                                    uint32_t cullingMask,
                                                    uint32_t& outOccluderCount,
                                                    float shadowSegmentSnapPixels);

    std::vector<glm::vec4> FilterDirectionalShadowSegmentsByFacing(const std::vector<glm::vec4>& shadowSegments, const glm::vec2& lightDirection);

    std::vector<ScreenDirectionalLight> BuildDirectionalLights(Scene& scene,
                                                                float interpolationAlpha,
                                                                const Camera& camera,
                                                                const glm::mat4& viewProjection,
                                                                uint32_t width,
                                                                uint32_t height,
                                                                float pixelsPerUnit,
                                                                uint32_t cullingMask);

    std::vector<ScreenPointLight> BuildPointLights(Scene& scene,
                                                    float interpolationAlpha,
                                                    const Camera& camera,
                                                    const glm::mat4& viewProjection,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    float pixelsPerUnit,
                                                    uint32_t cullingMask);

    // -------------------------------------------------------------------------
    // Render pass submission — defined in Lighting2DRendererPasses.cpp
    // -------------------------------------------------------------------------

    void SubmitSelectDrawBuffers(std::vector<uint32_t> attachments);
    void SubmitSelectGBufferDrawBuffers();
    void SubmitSelectAlbedoAttachmentOnly();
    void SubmitSelectNormalAndEntityAttachments();
    void SubmitClearNormalAttachment();
    void SubmitClearEntityIdAttachment();
    void SubmitClearCasterMaskAttachment();
    void SubmitClearCasterEntityIdAttachment();

    RenderPassDescriptor BuildGBufferRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                          uint32_t width,
                                                          uint32_t height);

    RenderPassDescriptor BuildLightAccumulationRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                                    uint32_t width,
                                                                    uint32_t height,
                                                                    const glm::vec3& ambientColor);

    RenderPassDescriptor BuildCompositeRenderPassDescriptor(const std::shared_ptr<Framebuffer>& framebuffer,
                                                            uint32_t width,
                                                            uint32_t height,
                                                            const glm::vec4& clearColor,
                                                            bool clearTarget);

    void SubmitNormalPassDraws(const std::vector<NormalPassSpriteDraw>& drawList, const glm::mat4& viewProjection);

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
                                    bool clampShadowToViewport,
                                    float maxCasterHeightPixels,
                                    bool hasAnyCasters);

    void SubmitPointLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                              const std::shared_ptr<Texture2D>& normalTexture,
                              const std::shared_ptr<Texture2D>& entityIdTexture,
                              const std::vector<ShadowSegment>& shadowSegments,
                              const ScreenPointLight& light,
                              uint32_t width,
                              uint32_t height,
                              float shadowSegmentSnapPixels);

    void SubmitCompositePass(const std::shared_ptr<Texture2D>& albedoTexture,
                             const std::shared_ptr<Texture2D>& lightTexture,
                             const glm::vec2& uvOffset,
                             const glm::vec2& uvScale);
}
