#include "Graphics/Lighting2DRendererInternal.h"

namespace Limitless::Lighting2DInternal
{
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
