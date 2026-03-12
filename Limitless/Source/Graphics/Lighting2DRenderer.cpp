#include "Graphics/Lighting2DRenderer.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Debug/Log.h"
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

namespace Limitless
{
    namespace
    {
        constexpr const char* kGBufferNormalShaderKey = "Assets/Shaders/Lighting2D_GBufferNormalPass.glsl";
        constexpr const char* kDirectionalLightShaderKey = "Assets/Shaders/Lighting2D_DirectionalLight.glsl";
        constexpr const char* kPointLightShaderKey = "Assets/Shaders/Lighting2D_PointLight.glsl";
        constexpr const char* kCompositeShaderKey = "Assets/Shaders/Lighting2D_Composite.glsl";

        constexpr uint32_t kShaderShadowSegmentCap = 128;
        constexpr uint32_t kPhysicsCircleSegmentApproximation = 12;
        constexpr float kEpsilon = 0.0001f;

        struct NormalPassSpriteDraw
        {
            glm::mat4 Model = glm::mat4(1.0f);
            glm::vec4 Color = glm::vec4(1.0f);
            float NormalStrength = 1.0f;
            bool ReceiveShadows = true;
            glm::vec2 CasterEntityId = glm::vec2(0.0f);
            std::shared_ptr<Texture2D> AlbedoTexture;
            std::shared_ptr<Texture2D> NormalTexture;
        };

        struct ShadowSegment
        {
            glm::vec4 Endpoints = glm::vec4(0.0f);
            glm::vec2 CasterEntityId = glm::vec2(0.0f);
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

            std::vector<ShadowSegment> CachedShadowSegments;
            uint32_t CachedShadowOccluderCount = 0;
            bool HasPreviousCameraRotation = false;
            glm::quat PreviousCameraRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            uint32_t ShadowFreezeFramesRemaining = 0;
        };

        Lighting2DRendererState* g_State = nullptr;

        std::string ToLowerCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool ShouldBypassDeferredLightingForDriver()
        {
            static bool s_Cached = false;
            static bool s_Bypass = false;
            static bool s_Logged = false;
            if (s_Cached)
                return s_Bypass;

            s_Cached = true;
            s_Bypass = false;

            if (!PlatformDetection::IsLinux())
                return false;

            std::string rendererName;
            std::string vendorName;
            if (auto api = GraphicsAPIDetector::GetAPI(GraphicsAPI::OpenGL); api.has_value())
            {
                rendererName = api->version.renderer;
                vendorName = api->version.vendor;
            }

            const std::string rendererLower = ToLowerCopy(rendererName);
            const std::string vendorLower = ToLowerCopy(vendorName);
            s_Bypass = rendererLower.find("svga3d") != std::string::npos ||
                       rendererLower.find("llvmpipe") != std::string::npos ||
                       vendorLower.find("vmware") != std::string::npos;

            if (s_Bypass && !s_Logged)
            {
                LT_CORE_WARN(
                    "Lighting2DRenderer: deferred lighting disabled for Linux OpenGL renderer '{}' (vendor '{}'); using fallback path",
                    rendererName.empty() ? "Unknown" : rendererName,
                    vendorName.empty() ? "Unknown" : vendorName);
                s_Logged = true;
            }

            return s_Bypass;
        }

        uint64_t MakeFramebufferSetKey(uint32_t width, uint32_t height)
        {
            return (static_cast<uint64_t>(width) << 32) | static_cast<uint64_t>(height);
        }

        int ClampShadowSamplesByQuality(const Lighting2DSettings& settings, int requestedSamples)
        {
            const int qualityCappedSamples = [&]() -> int {
                switch (settings.ShadowQualityLevel)
                {
                case 0: return 3;
                case 1: return 6;
                case 2:
                default: return std::max(1, settings.MaxShadowSamplesPerLight);
                }
            }();
            return std::clamp(requestedSamples, 1, std::max(1, qualityCappedSamples));
        }

        uint32_t ClampSegmentsByQuality(const Lighting2DSettings& settings)
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

        uint32_t ClampPointLightsByQuality(const Lighting2DSettings& settings)
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

        uint32_t ClampDirectionalLightsByQuality(const Lighting2DSettings& settings)
        {
            const uint32_t userMax = static_cast<uint32_t>(std::max(0, settings.MaxDirectionalLights));
            return std::min<uint32_t>(userMax, 8);
        }

        glm::vec2 EncodeEntityIdToUnitVec2(entt::entity entity)
        {
            if (entity == entt::null)
                return glm::vec2(0.0f);

            // 16-bit encoded entity id in two 8-bit channels (R,G).
            // This is robust on standard RGBA8 framebuffer attachments.
            const uint32_t raw = static_cast<uint32_t>(entt::to_integral(entity));
            const uint32_t id16 = (raw & 0xFFFFu);
            const float r = static_cast<float>(id16 & 0xFFu) / 255.0f;
            const float g = static_cast<float>((id16 >> 8) & 0xFFu) / 255.0f;
            return glm::vec2(r, g);
        }

        glm::quat ExtractCameraRotationFromViewMatrix(const glm::mat4& viewMatrix)
        {
            const glm::mat4 cameraWorld = glm::inverse(viewMatrix);
            glm::mat3 rotation = glm::mat3(cameraWorld);
            rotation[0] = glm::normalize(rotation[0]);
            rotation[1] = glm::normalize(rotation[1]);
            rotation[2] = glm::normalize(rotation[2]);
            return glm::normalize(glm::quat_cast(rotation));
        }

        float ComputeAngularVelocityDegreesPerSecond(const glm::quat& previousRotation,
                                                     const glm::quat& currentRotation,
                                                     float deltaTimeSeconds)
        {
            if (deltaTimeSeconds <= kEpsilon)
                return 0.0f;

            const float dotValue = std::clamp(std::abs(glm::dot(previousRotation, currentRotation)), 0.0f, 1.0f);
            const float deltaAngleRadians = 2.0f * std::acos(dotValue);
            return glm::degrees(deltaAngleRadians) / deltaTimeSeconds;
        }

        bool IsEntityInCanvasUiHierarchy(const entt::registry& registry, entt::entity entity)
        {
            if (!registry.all_of<RectTransformComponent>(entity))
                return false;

            entt::entity current = entity;
            while (current != entt::null)
            {
                if (registry.all_of<CanvasComponent>(current))
                    return true;

                const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
                if (!hierarchy || hierarchy->Parent == entt::null)
                    break;
                current = hierarchy->Parent;
            }

            return false;
        }

        bool IsEntityInLightHierarchy(const entt::registry& registry, entt::entity entity)
        {
            entt::entity current = entity;
            while (current != entt::null)
            {
                if (registry.any_of<DirectionalLight2DComponent, PointLight2DComponent>(current))
                    return true;

                const auto* hierarchy = registry.try_get<HierarchyComponent>(current);
                if (!hierarchy || hierarchy->Parent == entt::null)
                    break;
                current = hierarchy->Parent;
            }

            return false;
        }

        uint8_t GetEntityLayerForCameraCulling(const entt::registry& registry, entt::entity entity)
        {
            const auto* tag = registry.try_get<TagComponent>(entity);
            if (!tag)
                return 0;
            return static_cast<uint8_t>(std::min<int>(tag->Layer, 31));
        }

        uint32_t GetEffectiveCameraCullingMask(const Camera& camera)
        {
            if (camera.GetUsage() == CameraUsage::Editor)
                return ~0u;
            return SceneRenderer::GetActiveCullingMask();
        }

        bool IsEntityVisibleToCameraCullingMask(const entt::registry& registry, entt::entity entity, uint32_t cullingMask)
        {
            if (cullingMask == ~0u)
                return true;
            const uint8_t layer = GetEntityLayerForCameraCulling(registry, entity);
            return (cullingMask & (1u << layer)) != 0u;
        }

        bool ProjectWorldToScreen(const glm::mat4& viewProjection,
                                  const glm::vec3& worldPosition,
                                  uint32_t width,
                                  uint32_t height,
                                  glm::vec2& outScreenPosition)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            // Reject points behind (or too close to) the camera to avoid unstable
            // projection in fast strafe+rotate motion.
            if (clip.w <= kEpsilon)
                return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z))
                return false;
            outScreenPosition.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
            outScreenPosition.y = (ndc.y * 0.5f + 0.5f) * static_cast<float>(height);
            return true;
        }

        // Shadow-specific variant that never rejects.  When a point is behind
        // (or very close to) the camera, the clip-space W is clamped to a small
        // positive value so the perspective divide still produces a finite
        // screen position pushed far off-screen in the correct direction.
        // This prevents entire occluder polygons from being dropped when a
        // single corner grazes the near plane during camera rotation.
        glm::vec2 ProjectWorldToScreenClamped(const glm::mat4& viewProjection,
                                              const glm::vec3& worldPosition,
                                              uint32_t width,
                                              uint32_t height)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            const float safeW = std::max(clip.w, 0.001f);
            const float ndcX = clip.x / safeW;
            const float ndcY = clip.y / safeW;

            const float fWidth = static_cast<float>(width);
            const float fHeight = static_cast<float>(height);

            float sx = (ndcX * 0.5f + 0.5f) * fWidth;
            float sy = (ndcY * 0.5f + 0.5f) * fHeight;

            // Clamp to a generous off-screen range so shadow ray-segment
            // intersection math stays numerically stable.
            const float limit = std::max(fWidth, fHeight) * 4.0f;
            sx = std::clamp(sx, -limit, limit);
            sy = std::clamp(sy, -limit, limit);
            return { sx, sy };
        }

        float EstimatePixelsPerWorldUnit(const glm::mat4& viewMatrix,
                                         const glm::mat4& viewProjection,
                                         uint32_t width,
                                         uint32_t height)
        {
            // Estimate world->screen scale at the camera's intersection with
            // the Z=0 scene plane. This keeps directional shadow distance
            // stable in top-down perspective cameras where a global VP-only
            // estimate can wildly over-scale shadow length.
            glm::vec3 referencePoint = glm::vec3(0.0f);
            {
                const glm::mat4 cameraWorld = glm::inverse(viewMatrix);
                const glm::vec3 cameraPosition = glm::vec3(cameraWorld[3]);
                const glm::vec3 cameraForward = glm::normalize(-glm::vec3(cameraWorld[2]));
                if (std::abs(cameraForward.z) > kEpsilon)
                {
                    const float t = (0.0f - cameraPosition.z) / cameraForward.z;
                    if (t > 0.0f && std::isfinite(t))
                        referencePoint = cameraPosition + cameraForward * t;
                }
            }

            const glm::vec2 screenOrigin = ProjectWorldToScreenClamped(viewProjection, referencePoint, width, height);
            const glm::vec2 screenStepX = ProjectWorldToScreenClamped(viewProjection, referencePoint + glm::vec3(1.0f, 0.0f, 0.0f), width, height);
            const glm::vec2 screenStepY = ProjectWorldToScreenClamped(viewProjection, referencePoint + glm::vec3(0.0f, 1.0f, 0.0f), width, height);

            const float pixelsPerUnitX = glm::length(screenStepX - screenOrigin);
            const float pixelsPerUnitY = glm::length(screenStepY - screenOrigin);
            const float estimate = std::max(pixelsPerUnitX, pixelsPerUnitY);
            if (std::isfinite(estimate) && estimate > kEpsilon)
            {
                const float maxReasonable = static_cast<float>(std::max(width, height));
                return std::clamp(estimate, 1.0f, maxReasonable);
            }

            return static_cast<float>(std::max(width, height)) * 0.1f;
        }

        float ComputeInterpolationAlpha()
        {
            const float fixedDelta = Time::GetFixedDeltaTimeSeconds();
            if (fixedDelta <= 0.0f)
                return 1.0f;
            return std::clamp(Time::GetFixedTimeAccumulatorSeconds() / fixedDelta, 0.0f, 1.0f);
        }

        void EnsureShaderAssetLoaded(Assets::ShaderAsset::Ptr& shaderAsset, const char* shaderKey)
        {
            if (shaderAsset)
                return;

            shaderAsset = std::dynamic_pointer_cast<Assets::ShaderAsset>(Assets::AssetManager::GetCachedByKey(shaderKey));
            if (!shaderAsset)
            {
                static std::mutex s_PendingShaderLoadsMutex;
                static std::unordered_map<std::string, Async::Task<Assets::ShaderAsset::Ptr>> s_PendingShaderLoads;

                std::lock_guard<std::mutex> lock(s_PendingShaderLoadsMutex);
                auto pendingLoad = s_PendingShaderLoads.find(shaderKey);
                if (pendingLoad == s_PendingShaderLoads.end())
                {
                    s_PendingShaderLoads.emplace(
                        shaderKey,
                        Assets::AssetManager::LoadAsync<Assets::ShaderAsset>(shaderKey));
                }
                else if (pendingLoad->second.IsDone())
                {
                    try
                    {
                        shaderAsset = pendingLoad->second.Get();
                    }
                    catch (const std::exception& exception)
                    {
                        LT_WARN("Lighting2DRenderer shader async load failed for '{}': {}", shaderKey, exception.what());
                        shaderAsset.reset();
                    }
                    catch (...)
                    {
                        LT_WARN("Lighting2DRenderer shader async load failed for '{}' with unknown error", shaderKey);
                        shaderAsset.reset();
                    }

                    s_PendingShaderLoads.erase(pendingLoad);
                }
            }
        }

        std::shared_ptr<Shader> ResolveShaderFromAsset(Assets::ShaderAsset::Ptr& shaderAsset, const char* shaderKey)
        {
            EnsureShaderAssetLoaded(shaderAsset, shaderKey);
            if (!shaderAsset)
                return nullptr;
            return shaderAsset->GetShader();
        }

        void EnsureFallbackTextures()
        {
            if (!g_State->WhiteTexture)
            {
                const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
                TextureSpecification whiteSpecification{};
                whiteSpecification.GenerateMipmaps = false;
                whiteSpecification.MinFilter = TextureFilter::Nearest;
                whiteSpecification.MagFilter = TextureFilter::Nearest;
                whiteSpecification.WrapU = TextureWrap::ClampToEdge;
                whiteSpecification.WrapV = TextureWrap::ClampToEdge;
                g_State->WhiteTexture = Texture2D::CreateFromRGBA8(1, 1, whitePixel, whiteSpecification);
            }

            if (!g_State->FlatNormalTexture)
            {
                const uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };
                TextureSpecification normalSpecification{};
                normalSpecification.GenerateMipmaps = false;
                normalSpecification.MinFilter = TextureFilter::Nearest;
                normalSpecification.MagFilter = TextureFilter::Nearest;
                normalSpecification.WrapU = TextureWrap::ClampToEdge;
                normalSpecification.WrapV = TextureWrap::ClampToEdge;
                g_State->FlatNormalTexture = Texture2D::CreateFromRGBA8(1, 1, flatNormalPixel, normalSpecification);
            }
        }

        void EnsureQuadGeometryCreated()
        {
            if (g_State->UnitQuadVertexArray && g_State->UnitQuadVertexBuffer)
                return;

            const std::array<float, 16> quadVertices = {
                -0.5f, -0.5f, 0.0f, 0.0f,
                 0.5f, -0.5f, 1.0f, 0.0f,
                -0.5f,  0.5f, 0.0f, 1.0f,
                 0.5f,  0.5f, 1.0f, 1.0f
            };

            g_State->UnitQuadVertexArray = VertexArray::Create();
            g_State->UnitQuadVertexBuffer = VertexBuffer::Create(quadVertices.data(), static_cast<uint32_t>(sizeof(quadVertices)));
            g_State->UnitQuadVertexBuffer->SetLayout({
                { ShaderDataType::Float2, "a_Position" },
                { ShaderDataType::Float2, "a_UV" }
            });
            g_State->UnitQuadVertexArray->AddVertexBuffer(g_State->UnitQuadVertexBuffer);
        }

        bool EnsureFramebuffers(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return false;

            const auto createGBufferFramebuffer = [width, height]() -> std::shared_ptr<Framebuffer> {
                FramebufferSpecification gBufferSpec{};
                gBufferSpec.Width = width;
                gBufferSpec.Height = height;
                gBufferSpec.Samples = 1;
                gBufferSpec.ColorAttachmentCount = 3;
                gBufferSpec.DepthAttachment = true;
                gBufferSpec.StencilAttachment = false;
                gBufferSpec.SwapChainTarget = false;
                return Framebuffer::Create(gBufferSpec);
            };

            const auto createLightFramebuffer = [width, height]() -> std::shared_ptr<Framebuffer> {
                FramebufferSpecification lightSpec{};
                lightSpec.Width = width;
                lightSpec.Height = height;
                lightSpec.Samples = 1;
                lightSpec.ColorAttachmentCount = 1;
                lightSpec.DepthAttachment = false;
                lightSpec.StencilAttachment = false;
                lightSpec.SwapChainTarget = false;
                return Framebuffer::Create(lightSpec);
            };

            const uint64_t setKey = MakeFramebufferSetKey(width, height);
            auto& framebufferSet = g_State->FramebufferSets[setKey];
            if (!framebufferSet.GBuffer)
                framebufferSet.GBuffer = createGBufferFramebuffer();
            if (!framebufferSet.Light)
                framebufferSet.Light = createLightFramebuffer();

            if (!framebufferSet.GBuffer || !framebufferSet.Light)
                return false;

            // Safety check for stale entries (should be rare unless external resize occurred).
            if (framebufferSet.GBuffer->GetWidth() != width || framebufferSet.GBuffer->GetHeight() != height)
                framebufferSet.GBuffer = createGBufferFramebuffer();
            if (framebufferSet.Light->GetWidth() != width || framebufferSet.Light->GetHeight() != height)
                framebufferSet.Light = createLightFramebuffer();
            if (!framebufferSet.GBuffer || !framebufferSet.Light)
                return false;

            framebufferSet.LastUsedTick = ++g_State->FramebufferUseTick;

            constexpr size_t kMaxFramebufferSetCache = 4;
            if (g_State->FramebufferSets.size() > kMaxFramebufferSetCache)
            {
                auto evictIt = g_State->FramebufferSets.end();
                for (auto it = g_State->FramebufferSets.begin(); it != g_State->FramebufferSets.end(); ++it)
                {
                    if (it->first == setKey)
                        continue;
                    if (evictIt == g_State->FramebufferSets.end() || it->second.LastUsedTick < evictIt->second.LastUsedTick)
                        evictIt = it;
                }
                if (evictIt != g_State->FramebufferSets.end())
                    g_State->FramebufferSets.erase(evictIt);
            }

            g_State->GBufferFramebuffer = framebufferSet.GBuffer;
            g_State->LightFramebuffer = framebufferSet.Light;

            g_State->FramebufferWidth = width;
            g_State->FramebufferHeight = height;
            return true;
        }

        std::vector<entt::entity> BuildSortedSpriteRenderList(Scene& scene, float interpolationAlpha, uint32_t cullingMask)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<TransformComponent, SpriteComponent>();

            std::vector<entt::entity> entities;
            entities.reserve(view.size_hint());
            for (entt::entity entity : view)
            {
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;
                entities.push_back(entity);
            }

            std::sort(entities.begin(), entities.end(), [&scene, &registry, interpolationAlpha](entt::entity left, entt::entity right) {
                const auto& leftSprite = registry.get<SpriteComponent>(left);
                const auto& rightSprite = registry.get<SpriteComponent>(right);
                if (leftSprite.RenderOrder != rightSprite.RenderOrder)
                    return leftSprite.RenderOrder < rightSprite.RenderOrder;

                const glm::mat4 leftWorld = scene.GetWorldTransformMatrixForRendering(left, interpolationAlpha);
                const glm::mat4 rightWorld = scene.GetWorldTransformMatrixForRendering(right, interpolationAlpha);
                // Sort by world-space Z rather than view-space Z so the order stays
                // stable regardless of the editor camera orientation.
                const float leftWorldZ = leftWorld[3][2];
                const float rightWorldZ = rightWorld[3][2];
                constexpr float kDepthSortEpsilon = 0.005f;
                if (std::abs(leftWorldZ - rightWorldZ) > kDepthSortEpsilon)
                    return leftWorldZ < rightWorldZ;

                const auto* leftHierarchy = registry.try_get<HierarchyComponent>(left);
                const auto* rightHierarchy = registry.try_get<HierarchyComponent>(right);
                const int32_t leftOrder = leftHierarchy ? leftHierarchy->SiblingOrder : 0;
                const int32_t rightOrder = rightHierarchy ? rightHierarchy->SiblingOrder : 0;
                if (leftOrder != rightOrder)
                    return leftOrder < rightOrder;
                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });

            return entities;
        }

        void RefreshSpriteMaterialCache(MaterialComponent& material)
        {
            if (material.MaterialKey.empty())
                return;

            static std::mutex s_PendingMaterialLoadsMutex;
            static std::unordered_map<std::string, Async::Task<Assets::MaterialAsset::Ptr>> s_PendingMaterialLoads;

            if (!material.CachedMaterial && !material.MaterialLoadAttempted)
            {
                material.CachedMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(
                    Assets::AssetManager::GetCachedByKey(material.MaterialKey));
                if (!material.CachedMaterial)
                {
                    std::lock_guard<std::mutex> lock(s_PendingMaterialLoadsMutex);
                    if (!s_PendingMaterialLoads.contains(material.MaterialKey))
                    {
                        s_PendingMaterialLoads.emplace(
                            material.MaterialKey,
                            Assets::AssetManager::LoadAsync<Assets::MaterialAsset>(material.MaterialKey));
                    }
                }
                material.MaterialLoadAttempted = true;
            }
            else if (!material.CachedMaterial && material.MaterialLoadAttempted)
            {
                material.CachedMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(
                    Assets::AssetManager::GetCachedByKey(material.MaterialKey));
                if (!material.CachedMaterial)
                {
                    std::lock_guard<std::mutex> lock(s_PendingMaterialLoadsMutex);
                    auto pendingIt = s_PendingMaterialLoads.find(material.MaterialKey);
                    if (pendingIt != s_PendingMaterialLoads.end() && pendingIt->second.IsDone())
                    {
                        try
                        {
                            material.CachedMaterial = pendingIt->second.Get();
                        }
                        catch (const std::exception& exception)
                        {
                            LT_WARN("Lighting2DRenderer material async load failed for '{}': {}",
                                    material.MaterialKey,
                                    exception.what());
                            material.CachedMaterial.reset();
                        }
                        catch (...)
                        {
                            LT_WARN("Lighting2DRenderer material async load failed for '{}' with unknown error",
                                    material.MaterialKey);
                            material.CachedMaterial.reset();
                        }
                        s_PendingMaterialLoads.erase(pendingIt);
                    }
                }
            }
        }

        void RefreshSpriteTextureCache(SpriteComponent& sprite)
        {
            if (sprite.TextureKey.empty())
                return;

            static std::mutex s_PendingTextureLoadsMutex;
            static std::unordered_map<std::string, Async::Task<Assets::TextureAsset::Ptr>> s_PendingTextureLoads;

            if (!sprite.CachedTexture && !sprite.TextureLoadAttempted)
            {
                sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                if (!sprite.CachedTexture)
                {
                    std::lock_guard<std::mutex> lock(s_PendingTextureLoadsMutex);
                    if (!s_PendingTextureLoads.contains(sprite.TextureKey))
                    {
                        s_PendingTextureLoads.emplace(
                            sprite.TextureKey,
                            Assets::AssetManager::LoadAsync<Assets::TextureAsset>(sprite.TextureKey));
                    }
                }
                sprite.TextureLoadAttempted = true;
            }
            else if (!sprite.CachedTexture && sprite.TextureLoadAttempted)
            {
                sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
                if (!sprite.CachedTexture)
                {
                    std::lock_guard<std::mutex> lock(s_PendingTextureLoadsMutex);
                    auto pendingIt = s_PendingTextureLoads.find(sprite.TextureKey);
                    if (pendingIt != s_PendingTextureLoads.end() && pendingIt->second.IsDone())
                    {
                        try
                        {
                            sprite.CachedTexture = pendingIt->second.Get();
                        }
                        catch (const std::exception& exception)
                        {
                            LT_WARN("Lighting2DRenderer texture async load failed for '{}': {}",
                                    sprite.TextureKey,
                                    exception.what());
                            sprite.CachedTexture.reset();
                        }
                        catch (...)
                        {
                            LT_WARN("Lighting2DRenderer texture async load failed for '{}' with unknown error",
                                    sprite.TextureKey);
                            sprite.CachedTexture.reset();
                        }
                        s_PendingTextureLoads.erase(pendingIt);
                    }
                }
            }
        }

        std::vector<NormalPassSpriteDraw> BuildNormalPassDrawList(Scene& scene, float interpolationAlpha, uint32_t cullingMask)
        {
            std::vector<NormalPassSpriteDraw> drawList;
            auto sortedEntities = BuildSortedSpriteRenderList(scene, interpolationAlpha, cullingMask);
            drawList.reserve(sortedEntities.size());

            auto& registry = scene.GetRegistry();
            for (entt::entity entity : sortedEntities)
            {
                auto& sprite = registry.get<SpriteComponent>(entity);

                NormalPassSpriteDraw draw{};
                draw.Model = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                draw.Color = sprite.Color;
                draw.AlbedoTexture = g_State->WhiteTexture;
                draw.NormalTexture = g_State->FlatNormalTexture;
                draw.NormalStrength = 1.0f;
                draw.ReceiveShadows = sprite.ReceiveShadows;
                draw.CasterEntityId = EncodeEntityIdToUnitVec2(entity);

                bool hasMaterialButFailed = false;
                if (auto* material = registry.try_get<MaterialComponent>(entity))
                {
                    RefreshSpriteMaterialCache(*material);
                    if (material->CachedMaterial)
                    {
                        if (auto mainTexture = material->CachedMaterial->GetMainTexture())
                            draw.AlbedoTexture = mainTexture;

                        if (g_State->Settings.EnableNormalMaps)
                        {
                            if (auto normalTexture = material->CachedMaterial->GetNormalTexture())
                                draw.NormalTexture = normalTexture;
                            draw.NormalStrength = material->CachedMaterial->GetNormalStrength();
                        }
                    }
                    else if (!material->MaterialKey.empty())
                    {
                        hasMaterialButFailed = true;
                    }
                }

                if (!hasMaterialButFailed && draw.AlbedoTexture == g_State->WhiteTexture && !sprite.TextureKey.empty())
                {
                    RefreshSpriteTextureCache(sprite);
                    if (sprite.CachedTexture && sprite.CachedTexture->GetTexture())
                        draw.AlbedoTexture = sprite.CachedTexture->GetTexture();
                }

                if (hasMaterialButFailed)
                    draw.Color = glm::vec4(1.0f, 0.0f, 1.0f, sprite.Color.a);

                drawList.push_back(std::move(draw));
            }

            return drawList;
        }

        void SubmitSelectDrawBuffers(std::vector<uint32_t> attachments)
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<SetDrawColorAttachmentsCommand>(std::move(attachments)));
        }

        void SubmitSelectGBufferDrawBuffers()
        {
            SubmitSelectDrawBuffers({ 0u, 1u, 2u });
        }

        void SubmitSelectAlbedoAttachmentOnly()
        {
            SubmitSelectDrawBuffers({ 0u });
        }

        void SubmitSelectNormalAndEntityAttachments()
        {
            SubmitSelectDrawBuffers({ 1u, 2u });
        }

        void SubmitClearNormalAttachment()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(1u, glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)));
        }

        void SubmitClearEntityIdAttachment()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<ClearColorAttachmentCommand>(2u, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
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

        void EnsureLightingPipeline(std::shared_ptr<RenderPipeline>& pipeline,
                                    std::shared_ptr<Shader>& cachedShader,
                                    const std::shared_ptr<Shader>& shader,
                                    const char* debugName,
                                    bool blendEnabled,
                                    BlendFactor srcBlend,
                                    BlendFactor dstBlend,
                                    bool depthTestEnabled,
                                    bool depthWriteEnabled)
        {
            if (!shader || !g_State || !g_State->UnitQuadVertexBuffer)
                return;

            if (pipeline && cachedShader == shader)
                return;

            RenderPipelineDescriptor descriptor{};
            descriptor.DebugName = debugName ? debugName : "Lighting2D/Pipeline";
            descriptor.ShaderProgram = shader;
            descriptor.VertexLayout = g_State->UnitQuadVertexBuffer->GetLayout();
            descriptor.Topology = PrimitiveTopology::TriangleStrip;
            descriptor.BlendState.Enabled = blendEnabled;
            descriptor.BlendState.SourceColorFactor = srcBlend;
            descriptor.BlendState.DestinationColorFactor = dstBlend;
            descriptor.DepthStencilState.DepthTestEnabled = depthTestEnabled;
            descriptor.DepthStencilState.DepthWriteEnabled = depthWriteEnabled;
            descriptor.DepthStencilState.DepthCompare = DepthTestFunc::LessEqual;
            descriptor.RasterState.CullEnabled = false;
            descriptor.RasterState.CullMode = CullFace::Back;
            descriptor.RasterState.FillMode = PolygonMode::Fill;

            pipeline = RenderPipeline::Create(descriptor);
            cachedShader = shader;
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

            for (const NormalPassSpriteDraw& draw : drawList)
            {
                auto shaderRef = shader;
                auto vertexArrayRef = g_State->UnitQuadVertexArray;
                auto albedoRef = draw.AlbedoTexture ? draw.AlbedoTexture : g_State->WhiteTexture;
                auto normalRef = draw.NormalTexture ? draw.NormalTexture : g_State->FlatNormalTexture;
                const glm::mat4 model = draw.Model;
                const glm::vec4 color = draw.Color;
                const float normalStrength = draw.NormalStrength;
                const int receiveShadows = draw.ReceiveShadows ? 1 : 0;
                const glm::vec2 casterEntityId = draw.CasterEntityId;
                RenderBindingSet bindings{};
                bindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
                bindings.Textures.push_back(RenderTextureBinding{ "u_NormalTexture", normalRef, 1u });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_ViewProjection", viewProjection });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_Model", model });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_Color", color });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_NormalStrength", normalStrength });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_ReceiveShadows", static_cast<int32_t>(receiveShadows) });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_CasterEntityId", casterEntityId });
                bindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowAlphaCutoff", shadowAlphaCutoff });
                Renderer::GetInstance().SubmitCommand(std::make_unique<ApplyRenderBindingsCommand>(shaderRef, vertexArrayRef, std::move(bindings)));
                Renderer::GetInstance().SubmitCommand(std::make_unique<DrawArraysCommand>(DrawMode::TriangleStrip, 0, 4));
            }

            SubmitSelectGBufferDrawBuffers();
        }

        void BuildPhysicsOccluderPolygon(const entt::registry& registry, entt::entity entity, std::vector<glm::vec2>& outPoints)
        {
            outPoints.clear();
            if (const auto* boxCollider = registry.try_get<BoxCollider2DComponent>(entity))
            {
                const glm::vec2 halfSize = boxCollider->Size * 0.5f;
                outPoints.push_back(boxCollider->Offset + glm::vec2(-halfSize.x, -halfSize.y));
                outPoints.push_back(boxCollider->Offset + glm::vec2(halfSize.x, -halfSize.y));
                outPoints.push_back(boxCollider->Offset + glm::vec2(halfSize.x, halfSize.y));
                outPoints.push_back(boxCollider->Offset + glm::vec2(-halfSize.x, halfSize.y));
                return;
            }

            if (const auto* circleCollider = registry.try_get<CircleCollider2DComponent>(entity))
            {
                const float radius = std::max(0.01f, circleCollider->Radius);
                outPoints.reserve(kPhysicsCircleSegmentApproximation);
                for (uint32_t index = 0; index < kPhysicsCircleSegmentApproximation; ++index)
                {
                    const float phase = static_cast<float>(index) / static_cast<float>(kPhysicsCircleSegmentApproximation);
                    const float angle = phase * glm::two_pi<float>();
                    const glm::vec2 direction(std::cos(angle), std::sin(angle));
                    outPoints.push_back(circleCollider->Offset + direction * radius);
                }
            }
        }

        std::vector<glm::vec2> ResolveOccluderLocalPolygon(const entt::registry& registry, entt::entity entity, const ShadowOccluder2DComponent& occluder)
        {
            std::vector<glm::vec2> points;
            if (occluder.Source == ShadowOccluder2DComponent::SourceMode::PhysicsCollider)
            {
                BuildPhysicsOccluderPolygon(registry, entity, points);
            }
            else
            {
                points = occluder.PolygonPoints;
            }

            if (points.empty())
                return points;

            if (occluder.Extrusion > 0.0f)
            {
                glm::vec2 centroid(0.0f);
                for (const glm::vec2& point : points)
                    centroid += point;
                centroid /= static_cast<float>(points.size());
                for (glm::vec2& point : points)
                {
                    const glm::vec2 direction = point - centroid;
                    const float length = glm::length(direction);
                    if (length > kEpsilon)
                        point += (direction / length) * occluder.Extrusion;
                }
            }

            return points;
        }

        std::vector<ShadowSegment> BuildShadowSegments(Scene& scene,
                                                       float interpolationAlpha,
                                                       const glm::mat4& viewProjection,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       uint32_t maxSegments,
                                                       uint32_t cullingMask,
                                                       uint32_t& outOccluderCount,
                                                       float shadowSegmentSnapPixels)
        {
            std::vector<ShadowSegment> segments;
            segments.reserve(maxSegments);
            outOccluderCount = 0;
            const float snappedPixels = std::max(0.0f, shadowSegmentSnapPixels);

            auto& registry = scene.GetRegistry();
            std::unordered_set<entt::entity> explicitOccluderEntities;
            explicitOccluderEntities.reserve(64);

            auto appendOccluderSegments = [&](entt::entity entity, const std::vector<glm::vec2>& localPoints, bool closed) {
                if (segments.size() >= maxSegments || localPoints.size() < 2)
                    return;

                const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                const glm::vec2 casterEntityId = EncodeEntityIdToUnitVec2(entity);

                // Work in clip space so we can clip edges at the near plane
                // instead of clamping vertices to extreme off-screen positions.
                // Clamping caused discontinuous segment jumps when clip.w
                // crossed the threshold, which was the main source of shadow
                // flicker during camera rotation.
                std::vector<glm::vec4> clipPoints;
                clipPoints.reserve(localPoints.size());
                for (const glm::vec2& localPoint : localPoints)
                {
                    const glm::vec4 worldPoint = worldTransform * glm::vec4(localPoint, 0.0f, 1.0f);
                    clipPoints.push_back(viewProjection * worldPoint);
                }

                if (closed && clipPoints.size() >= 3)
                {
                    // Compute signed area from world-space XY so the winding
                    // stays stable regardless of perspective camera orientation.
                    float worldSignedArea = 0.0f;
                    for (size_t index = 0; index < localPoints.size(); ++index)
                    {
                        const glm::vec4 wa = worldTransform * glm::vec4(localPoints[index], 0.0f, 1.0f);
                        const glm::vec4 wb = worldTransform * glm::vec4(localPoints[(index + 1) % localPoints.size()], 0.0f, 1.0f);
                        worldSignedArea += (wa.x * wb.y) - (wb.x * wa.y);
                    }

                    if (worldSignedArea < 0.0f)
                        std::reverse(clipPoints.begin(), clipPoints.end());
                }

                ++outOccluderCount;

                constexpr float kNearW = 0.01f;
                const float fWidth = static_cast<float>(width);
                const float fHeight = static_cast<float>(height);

                // Project a clip-space point (with w >= kNearW) to screen space.
                // Coordinates are clamped to a generous off-screen range to
                // keep the shader's ray-segment intersection numerically stable.
                // For visible pixels the intersection point is always near the
                // on-screen segment endpoint, so this clamp does not affect
                // shadow correctness on screen.
                const float coordLimit = std::max(fWidth, fHeight) * 4.0f;
                auto projectToScreen = [fWidth, fHeight, snappedPixels, coordLimit](const glm::vec4& clip) -> glm::vec2 {
                    const float ndcX = clip.x / clip.w;
                    const float ndcY = clip.y / clip.w;
                    float sx = (ndcX * 0.5f + 0.5f) * fWidth;
                    float sy = (ndcY * 0.5f + 0.5f) * fHeight;
                    sx = std::clamp(sx, -coordLimit, coordLimit);
                    sy = std::clamp(sy, -coordLimit, coordLimit);
                    if (snappedPixels > kEpsilon)
                    {
                        sx = std::round(sx / snappedPixels) * snappedPixels;
                        sy = std::round(sy / snappedPixels) * snappedPixels;
                    }
                    return { sx, sy };
                };

                // Clip a single edge to the camera near plane and emit as a
                // screen-space segment.  When one vertex is behind the camera
                // the edge is smoothly clipped at the near plane boundary
                // (interpolating along the edge) instead of clamping the vertex
                // to an extreme coordinate.
                auto addClippedEdge = [&](const glm::vec4& clipA, const glm::vec4& clipB) {
                    if (segments.size() >= maxSegments)
                        return;

                    const bool behindA = clipA.w < kNearW;
                    const bool behindB = clipB.w < kNearW;

                    if (behindA && behindB)
                        return;

                    glm::vec4 ca = clipA;
                    glm::vec4 cb = clipB;

                    if (behindA)
                    {
                        const float t = (kNearW - ca.w) / (cb.w - ca.w);
                        ca = glm::mix(ca, cb, t);
                    }
                    else if (behindB)
                    {
                        const float t = (kNearW - cb.w) / (ca.w - cb.w);
                        cb = glm::mix(cb, ca, t);
                    }

                    const glm::vec2 screenA = projectToScreen(ca);
                    const glm::vec2 screenB = projectToScreen(cb);
                    const float edgeLength = glm::length(screenB - screenA);
                    if (edgeLength <= kEpsilon)
                        return;

                    segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId });
                };

                for (size_t index = 0; index + 1 < clipPoints.size(); ++index)
                    addClippedEdge(clipPoints[index], clipPoints[index + 1]);

                if (closed && clipPoints.size() >= 3 && segments.size() < maxSegments)
                    addClippedEdge(clipPoints.back(), clipPoints.front());
            };

            auto appendWorldEdge = [&](const glm::vec3& worldStart, const glm::vec3& worldEnd, const glm::vec2& casterEntityId) {
                if (segments.size() >= maxSegments)
                    return;

                constexpr float kNearW = 0.01f;
                const glm::vec4 clipA = viewProjection * glm::vec4(worldStart, 1.0f);
                const glm::vec4 clipB = viewProjection * glm::vec4(worldEnd, 1.0f);

                const bool behindA = clipA.w < kNearW;
                const bool behindB = clipB.w < kNearW;
                if (behindA && behindB)
                    return;

                glm::vec4 ca = clipA;
                glm::vec4 cb = clipB;
                if (behindA)
                {
                    const float t = (kNearW - ca.w) / (cb.w - ca.w);
                    ca = glm::mix(ca, cb, t);
                }
                else if (behindB)
                {
                    const float t = (kNearW - cb.w) / (ca.w - cb.w);
                    cb = glm::mix(cb, ca, t);
                }

                const float fWidth = static_cast<float>(width);
                const float fHeight = static_cast<float>(height);
                const float coordLimit = std::max(fWidth, fHeight) * 4.0f;
                auto projectToScreen = [fWidth, fHeight, snappedPixels, coordLimit](const glm::vec4& clip) -> glm::vec2 {
                    const float ndcX = clip.x / clip.w;
                    const float ndcY = clip.y / clip.w;
                    float sx = (ndcX * 0.5f + 0.5f) * fWidth;
                    float sy = (ndcY * 0.5f + 0.5f) * fHeight;
                    sx = std::clamp(sx, -coordLimit, coordLimit);
                    sy = std::clamp(sy, -coordLimit, coordLimit);
                    if (snappedPixels > kEpsilon)
                    {
                        sx = std::round(sx / snappedPixels) * snappedPixels;
                        sy = std::round(sy / snappedPixels) * snappedPixels;
                    }
                    return { sx, sy };
                };

                const glm::vec2 screenA = projectToScreen(ca);
                const glm::vec2 screenB = projectToScreen(cb);
                if (glm::length(screenB - screenA) <= kEpsilon)
                    return;

                segments.push_back(ShadowSegment{ glm::vec4(screenA.x, screenA.y, screenB.x, screenB.y), casterEntityId });
            };

            auto occluderView = registry.view<ShadowOccluder2DComponent>();
            for (entt::entity entity : occluderView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;

                auto& occluder = occluderView.get<ShadowOccluder2DComponent>(entity);
                explicitOccluderEntities.insert(entity);
                if (!occluder.Enabled)
                    continue;
                if (const auto* sprite = registry.try_get<SpriteComponent>(entity); sprite && !sprite->CastShadows)
                    continue;

                std::vector<glm::vec2> localPoints = ResolveOccluderLocalPolygon(registry, entity, occluder);
                appendOccluderSegments(entity, localPoints, occluder.Closed);
            }

            auto shouldAutoCastFromSprite = [&](const SpriteComponent& sprite) -> bool {
                if (!sprite.CastShadows || sprite.Color.a <= 0.01f)
                    return false;
                return true;
            };

            // Hybrid fallback: collider-backed occluders for entities without an explicit ShadowOccluder2D.
            // This keeps scene authoring fast while preserving explicit component control when present.
            auto boxColliderView = registry.view<BoxCollider2DComponent>();
            for (entt::entity entity : boxColliderView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (explicitOccluderEntities.contains(entity))
                    continue;
                if (IsEntityInLightHierarchy(registry, entity))
                    continue;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;
                const auto* sprite = registry.try_get<SpriteComponent>(entity);
                if (!sprite)
                    continue;
                if (!shouldAutoCastFromSprite(*sprite))
                    continue;

                std::vector<glm::vec2> localPoints;
                BuildPhysicsOccluderPolygon(registry, entity, localPoints);
                appendOccluderSegments(entity, localPoints, true);
            }

            auto circleColliderView = registry.view<CircleCollider2DComponent>();
            for (entt::entity entity : circleColliderView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (explicitOccluderEntities.contains(entity) || registry.all_of<BoxCollider2DComponent>(entity))
                    continue;
                if (IsEntityInLightHierarchy(registry, entity))
                    continue;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;
                const auto* sprite = registry.try_get<SpriteComponent>(entity);
                if (!sprite)
                    continue;
                if (!shouldAutoCastFromSprite(*sprite))
                    continue;

                std::vector<glm::vec2> localPoints;
                BuildPhysicsOccluderPolygon(registry, entity, localPoints);
                appendOccluderSegments(entity, localPoints, true);
            }

            // Sprite fallback: allow regular sprite entities to cast shadows even
            // without explicit ShadowOccluder2D/physics collider components.
            // The shadow shape is a unit quad scaled by the entity transform, so
            // its size matches the rendered sprite.  Shadow *length* is governed
            // by the per-light ShadowDistance setting -- keep that value reasonable
            // (e.g. 2-10 world units) for top-down games.
            auto spriteView = registry.view<TransformComponent, SpriteComponent>();
            for (entt::entity entity : spriteView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (explicitOccluderEntities.contains(entity))
                    continue;
                if (IsEntityInLightHierarchy(registry, entity))
                    continue;
                if (registry.any_of<BoxCollider2DComponent, CircleCollider2DComponent>(entity))
                    continue;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;
                if (IsEntityInCanvasUiHierarchy(registry, entity))
                    continue;

                const auto& sprite = spriteView.get<SpriteComponent>(entity);
                if (!shouldAutoCastFromSprite(sprite))
                    continue;

                // Automatic sprite fallback should not produce map-sized caster
                // quads when users scale sprites up for visual reasons (common
                // with pixel-art imports). Clamp fallback caster extents in
                // world space; explicit colliders/occluders remain exact.
                const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                const float worldScaleX = std::max(glm::length(glm::vec2(worldTransform[0].x, worldTransform[0].y)), kEpsilon);
                const float worldScaleY = std::max(glm::length(glm::vec2(worldTransform[1].x, worldTransform[1].y)), kEpsilon);

                constexpr float kAutoSpriteCasterMaxWorldExtent = 1.5f;
                const float clampedWorldWidth = std::min(worldScaleX, kAutoSpriteCasterMaxWorldExtent);
                const float clampedWorldHeight = std::min(worldScaleY, kAutoSpriteCasterMaxWorldExtent);

                const glm::vec2 localHalfExtents(
                    0.5f * clampedWorldWidth / worldScaleX,
                    0.5f * clampedWorldHeight / worldScaleY);
                const std::vector<glm::vec2> localQuad = {
                    glm::vec2(-localHalfExtents.x, -localHalfExtents.y),
                    glm::vec2( localHalfExtents.x, -localHalfExtents.y),
                    glm::vec2( localHalfExtents.x,  localHalfExtents.y),
                    glm::vec2(-localHalfExtents.x,  localHalfExtents.y)
                };
                appendOccluderSegments(entity, localQuad, true);
            }

            // Tilemap fallback: emit edge segments for occupied cells in Grid2D
            // layers so tilemaps can cast directional/point shadows without
            // requiring explicit collider/occluder authoring.
            auto gridView = registry.view<TransformComponent, Grid2DComponent>();
            for (entt::entity gridEntity : gridView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (!scene.IsEntityEnabledInHierarchy(gridEntity))
                    continue;

                const auto& grid = gridView.get<Grid2DComponent>(gridEntity);
                const glm::vec2 cellSize(
                    std::max(0.001f, grid.CellSize.x),
                    std::max(0.001f, grid.CellSize.y));
                const glm::mat4 gridWorldTransform = scene.GetWorldTransformMatrixForRendering(gridEntity, interpolationAlpha);

                const auto children = scene.GetChildren(gridEntity);
                for (entt::entity layerEntity : children)
                {
                    if (segments.size() >= maxSegments)
                        break;
                    if (!registry.all_of<TilemapLayerComponent>(layerEntity) ||
                        !scene.IsEntityEnabledInHierarchy(layerEntity))
                    {
                        continue;
                    }
                    if (!IsEntityVisibleToCameraCullingMask(registry, layerEntity, cullingMask))
                        continue;

                    auto& layer = registry.get<TilemapLayerComponent>(layerEntity);
                    // Tilemap layers must opt in to shadow casting. Keeping
                    // this separate from collision avoids giant map-perimeter
                    // shadows when a gameplay collision layer is broadly filled.
                    if (!layer.CollisionEnabled || !layer.CastShadows)
                        continue;
                    EnsureTilemapLayerStorage(grid, layer);
                    const int32_t widthCells = std::max(1, grid.GridSize.x);
                    const int32_t heightCells = std::max(1, grid.GridSize.y);
                    const glm::vec2 firstCellCenter = GetTilemapLayerFirstCellCenter(grid, layer);

                    const auto hasTileAt = [&](int32_t cellX, int32_t cellY) -> bool {
                        if (cellX < 0 || cellY < 0 || cellX >= widthCells || cellY >= heightCells)
                            return false;
                        const size_t index = static_cast<size_t>(cellY * widthCells + cellX);
                        if (index >= layer.Tiles.size())
                            return false;
                        return layer.Tiles[index] != 0u;
                    };

                    bool emittedAnySegment = false;
                    auto emitLocalEdge = [&](const glm::vec2& localA, const glm::vec2& localB) {
                        const glm::vec4 worldAH = gridWorldTransform * glm::vec4(localA, 0.0f, 1.0f);
                        const glm::vec4 worldBH = gridWorldTransform * glm::vec4(localB, 0.0f, 1.0f);
                        const size_t beforeCount = segments.size();
                        appendWorldEdge(glm::vec3(worldAH), glm::vec3(worldBH), EncodeEntityIdToUnitVec2(layerEntity));
                        if (segments.size() > beforeCount)
                            emittedAnySegment = true;
                    };

                    for (int32_t cellY = 0; cellY < heightCells && segments.size() < maxSegments; ++cellY)
                    {
                        for (int32_t cellX = 0; cellX < widthCells && segments.size() < maxSegments; ++cellX)
                        {
                            if (!hasTileAt(cellX, cellY))
                                continue;

                            const glm::vec2 localCenter = firstCellCenter + glm::vec2(
                                static_cast<float>(cellX) * cellSize.x,
                                static_cast<float>(cellY) * cellSize.y);
                            const glm::vec2 localMin = localCenter - cellSize * 0.5f;
                            const glm::vec2 localMax = localCenter + cellSize * 0.5f;

                            // Maintain consistent counter-clockwise edge winding for
                            // stable outward normals in shader-side facing tests.
                            if (!hasTileAt(cellX, cellY - 1))
                                emitLocalEdge(glm::vec2(localMin.x, localMin.y), glm::vec2(localMax.x, localMin.y));
                            if (!hasTileAt(cellX + 1, cellY))
                                emitLocalEdge(glm::vec2(localMax.x, localMin.y), glm::vec2(localMax.x, localMax.y));
                            if (!hasTileAt(cellX, cellY + 1))
                                emitLocalEdge(glm::vec2(localMax.x, localMax.y), glm::vec2(localMin.x, localMax.y));
                            if (!hasTileAt(cellX - 1, cellY))
                                emitLocalEdge(glm::vec2(localMin.x, localMax.y), glm::vec2(localMin.x, localMin.y));
                        }
                    }

                    if (emittedAnySegment)
                        ++outOccluderCount;
                }
            }

            return segments;
        }

        std::vector<glm::vec4> FilterDirectionalShadowSegmentsByFacing(const std::vector<glm::vec4>& shadowSegments, const glm::vec2& lightDirection)
        {
            if (shadowSegments.empty() || glm::length(lightDirection) <= kEpsilon)
                return shadowSegments;

            const glm::vec2 rayDirection = -glm::normalize(lightDirection);
            std::vector<glm::vec4> filteredSegments;
            filteredSegments.reserve(shadowSegments.size());
            for (const glm::vec4& segment : shadowSegments)
            {
                const glm::vec2 start(segment.x, segment.y);
                const glm::vec2 end(segment.z, segment.w);
                const glm::vec2 edge = end - start;
                if (glm::length(edge) <= kEpsilon)
                    continue;

                const glm::vec2 outwardNormal = glm::normalize(glm::vec2(edge.y, -edge.x));
                if (glm::dot(outwardNormal, rayDirection) > 0.0f)
                    filteredSegments.push_back(segment);
            }

            // Fallback to unfiltered segments if winding input is inconsistent.
            if (filteredSegments.empty())
                return shadowSegments;
            return filteredSegments;
        }

        std::vector<ScreenDirectionalLight> BuildDirectionalLights(Scene& scene,
                                                                   float interpolationAlpha,
                                                                   const Camera& camera,
                                                                   const glm::mat4& viewProjection,
                                                                   uint32_t width,
                                                                   uint32_t height,
                                                                   float pixelsPerUnit,
                                                                   uint32_t cullingMask)
        {
            std::vector<ScreenDirectionalLight> lights;
            lights.reserve(std::max(0, g_State->Settings.MaxDirectionalLights));

            const uint32_t maxDirectionalLights = ClampDirectionalLightsByQuality(g_State->Settings);
            if (maxDirectionalLights == 0)
                return lights;

            auto& registry = scene.GetRegistry();
            auto directionalView = registry.view<DirectionalLight2DComponent>();
            for (entt::entity entity : directionalView)
            {
                if (lights.size() >= maxDirectionalLights)
                    break;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;

                auto& directional = directionalView.get<DirectionalLight2DComponent>(entity);
                if (!directional.Enabled || directional.Intensity <= 0.0f)
                    continue;

                glm::vec2 worldDirection = directional.Direction;
                if (directional.UseEntityRotation)
                {
                    const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                    worldDirection = glm::vec2(worldTransform[0].x, worldTransform[0].y);
                }
                if (glm::length(worldDirection) <= kEpsilon)
                    worldDirection = glm::vec2(0.0f, -1.0f);
                worldDirection = glm::normalize(worldDirection);

                glm::vec4 viewDirection4 = camera.GetViewMatrix() * glm::vec4(worldDirection, 0.0f, 0.0f);
                glm::vec2 screenDirection(viewDirection4.x, viewDirection4.y);
                if (glm::length(screenDirection) <= kEpsilon)
                {
                    const glm::vec2 originScreen = ProjectWorldToScreenClamped(viewProjection, glm::vec3(0.0f, 0.0f, 0.0f), width, height);
                    const glm::vec2 endScreen = ProjectWorldToScreenClamped(viewProjection, glm::vec3(worldDirection, 0.0f), width, height);
                    screenDirection = endScreen - originScreen;
                }
                if (glm::length(screenDirection) <= kEpsilon)
                    screenDirection = glm::vec2(0.0f, -1.0f);
                screenDirection = glm::normalize(screenDirection);

                directional.RuntimeResolvedDirection = worldDirection;

                ScreenDirectionalLight screenLight{};
                screenLight.Color = glm::max(directional.Color, glm::vec3(0.0f));
                screenLight.Intensity = directional.Intensity;
                screenLight.ShadowDirection = screenDirection;
                screenLight.ShadingDirection = worldDirection;
                screenLight.CastShadows = g_State->Settings.EnableShadows && directional.CastShadows;
                screenLight.ShadowStrength = std::clamp(directional.ShadowStrength, 0.0f, 1.0f);
                screenLight.ShadowSoftnessPixels = std::max(0.0f, directional.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * pixelsPerUnit);
                screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State->Settings, directional.ShadowSamples);
                screenLight.ShadowDistancePixels = std::max(1.0f, directional.ShadowDistance * pixelsPerUnit);
                // Screen-space directional ray tests need a bias to avoid self-occlusion on receiver quads.
                const float biasScale = std::max(0.0f, g_State->Settings.DirectionalShadowBiasScale);
                screenLight.ShadowBiasPixels = std::max(0.0f, directional.ShadowBias * biasScale * pixelsPerUnit);
                lights.push_back(screenLight);
            }

            return lights;
        }

        std::vector<ScreenPointLight> BuildPointLights(Scene& scene,
                                                       float interpolationAlpha,
                                                       const glm::mat4& viewProjection,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       float pixelsPerUnit,
                                                       uint32_t cullingMask)
        {
            std::vector<ScreenPointLight> lights;
            lights.reserve(std::max(0, g_State->Settings.MaxPointLights));

            const uint32_t maxPointLights = ClampPointLightsByQuality(g_State->Settings);
            if (maxPointLights == 0)
                return lights;

            auto& registry = scene.GetRegistry();
            auto pointView = registry.view<PointLight2DComponent, TransformComponent>();
            for (entt::entity entity : pointView)
            {
                if (lights.size() >= maxPointLights)
                    break;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (!IsEntityVisibleToCameraCullingMask(registry, entity, cullingMask))
                    continue;

                auto& pointLight = pointView.get<PointLight2DComponent>(entity);
                if (!pointLight.Enabled || pointLight.Intensity <= 0.0f || pointLight.Radius <= 0.01f)
                    continue;

                const glm::mat4 worldTransform = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                const glm::vec3 worldPosition = glm::vec3(worldTransform[3]);

                // Use the clamped projection so lights near the camera's near
                // plane are pushed off-screen instead of being dropped entirely.
                // A dropped light causes the lit area to flash to ambient-only
                // for one frame, which is the primary cause of flicker during
                // combined strafe + rotation.
                const glm::vec2 screenPosition = ProjectWorldToScreenClamped(viewProjection, worldPosition, width, height);

                const glm::vec3 worldPositionRadius = glm::vec3(worldTransform * glm::vec4(pointLight.Radius, 0.0f, 0.0f, 1.0f));
                const glm::vec2 screenRadiusPosition = ProjectWorldToScreenClamped(viewProjection, worldPositionRadius, width, height);
                float radiusPixels = glm::length(screenRadiusPosition - screenPosition);
                if (radiusPixels < 1.0f)
                    radiusPixels = pointLight.Radius * pixelsPerUnit;

                ScreenPointLight screenLight{};
                screenLight.Color = glm::max(pointLight.Color, glm::vec3(0.0f));
                screenLight.Intensity = pointLight.Intensity;
                screenLight.Position = screenPosition;
                screenLight.RadiusPixels = std::max(1.0f, radiusPixels);
                screenLight.Falloff = std::max(0.1f, pointLight.Falloff);
                screenLight.CastShadows = g_State->Settings.EnableShadows && pointLight.CastShadows;
                screenLight.ShadowStrength = std::clamp(pointLight.ShadowStrength, 0.0f, 1.0f);
                screenLight.ShadowSoftnessPixels = std::max(0.0f, pointLight.ShadowSoftness * g_State->Settings.ShadowSoftnessScale * pixelsPerUnit);
                screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State->Settings, pointLight.ShadowSamples);
                screenLight.ShadowBiasPixels = std::max(0.0f, pointLight.ShadowBias * pixelsPerUnit);
                lights.push_back(screenLight);
            }

            return lights;
        }

        void SubmitDirectionalLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                                        const std::shared_ptr<Texture2D>& normalTexture,
                                        const std::shared_ptr<Texture2D>& entityIdTexture,
                                        const std::vector<ShadowSegment>& shadowSegments,
                                        const ScreenDirectionalLight& light,
                                        uint32_t width,
                                        uint32_t height,
                                        float shadowSegmentSnapPixels)
        {
            auto shader = ResolveShaderFromAsset(g_State->DirectionalLightShaderAsset, kDirectionalLightShaderKey);
            if (!shader || !g_State->UnitQuadVertexArray)
                return;

            auto shaderRef = shader;
            auto vertexArrayRef = g_State->UnitQuadVertexArray;
            auto albedoRef = albedoTexture;
            auto normalRef = normalTexture;
            auto entityIdRef = entityIdTexture;
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
            // Use all shadow segments without screen-space facing filter.
            // The ray-segment intersection in the shader is geometrically
            // correct regardless of edge orientation, so the filter was purely
            // a performance optimization.  Under perspective camera rotation
            // the screen-space edge normals shift rapidly, causing the filter
            // to pop edges in/out and produce shadow flicker.
            const size_t segmentCountClamped = std::min<size_t>(shadowSegments.size(), kShaderShadowSegmentCap);
            std::vector<glm::vec4> segmentEndpoints;
            std::vector<glm::vec2> segmentCasterIds;
            segmentEndpoints.reserve(segmentCountClamped);
            segmentCasterIds.reserve(segmentCountClamped);
            for (size_t i = 0; i < segmentCountClamped; ++i)
            {
                segmentEndpoints.push_back(shadowSegments[i].Endpoints);
                segmentCasterIds.push_back(shadowSegments[i].CasterEntityId);
            }

            RenderBindingSet directionalBindings{};
            directionalBindings.Textures.push_back(RenderTextureBinding{ "u_AlbedoTexture", albedoRef, 0u });
            directionalBindings.Textures.push_back(RenderTextureBinding{ "u_NormalTexture", normalRef, 1u });
            directionalBindings.Textures.push_back(RenderTextureBinding{ "u_EntityIdTexture", entityIdRef, 2u });
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
            directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentSnapPixels", shadowSegmentSnapPixelsClamped });
            directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCount", static_cast<int32_t>(segmentEndpoints.size()) });
            directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegments", std::move(segmentEndpoints) });
            directionalBindings.Parameters.push_back(RenderParameterBinding{ "u_ShadowSegmentCasterIds", std::move(segmentCasterIds) });
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
            std::vector<glm::vec2> segmentCasterIds;
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

        bool PrepareResources(uint32_t width, uint32_t height)
        {
            EnsureFallbackTextures();
            EnsureQuadGeometryCreated();
            if (!EnsureFramebuffers(width, height))
                return false;

            if (!ResolveShaderFromAsset(g_State->GBufferNormalShaderAsset, kGBufferNormalShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State->DirectionalLightShaderAsset, kDirectionalLightShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State->PointLightShaderAsset, kPointLightShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State->CompositeShaderAsset, kCompositeShaderKey))
                return false;

            return g_State->GBufferFramebuffer &&
                   g_State->LightFramebuffer &&
                   g_State->UnitQuadVertexArray &&
                   g_State->WhiteTexture &&
                   g_State->FlatNormalTexture;
        }
    }

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
        std::vector<NormalPassSpriteDraw> normalPassDraws = BuildNormalPassDrawList(scene, interpolationAlpha, cullingMask);
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
            shadowSegments = BuildShadowSegments(scene, interpolationAlpha, viewProjection, width, height, maxShadowSegments, cullingMask, occluderCount, effectiveShadowSegmentSnapPixels);
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
            SubmitDirectionalLightPass(gBufferAlbedo, gBufferNormal, gBufferEntityId, shadowSegments, directionalLight, width, height, effectiveShadowSegmentSnapPixels);
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

