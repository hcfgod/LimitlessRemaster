#include "Graphics/Lighting2DRenderer.h"

#include "Assets/AssetManager.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Time.h"
#include "Graphics/Buffer.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/OpenGL/OpenGLShader.h"
#include "Graphics/OpenGL/OpenGLTexture.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include "Graphics/VertexArray.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#define LT_USE_GLAD
#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
            std::shared_ptr<Texture2D> AlbedoTexture;
            std::shared_ptr<Texture2D> NormalTexture;
        };

        struct ScreenDirectionalLight
        {
            glm::vec3 Color = glm::vec3(1.0f);
            float Intensity = 1.0f;
            glm::vec2 Direction = glm::vec2(0.0f, -1.0f);
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

            std::shared_ptr<VertexArray> UnitQuadVertexArray;
            std::shared_ptr<VertexBuffer> UnitQuadVertexBuffer;

            std::shared_ptr<Texture2D> WhiteTexture;
            std::shared_ptr<Texture2D> FlatNormalTexture;

            Assets::ShaderAsset::Ptr GBufferNormalShaderAsset;
            Assets::ShaderAsset::Ptr DirectionalLightShaderAsset;
            Assets::ShaderAsset::Ptr PointLightShaderAsset;
            Assets::ShaderAsset::Ptr CompositeShaderAsset;

            std::vector<glm::vec4> CachedShadowSegments;
            uint32_t CachedShadowOccluderCount = 0;
            bool HasPreviousCameraRotation = false;
            glm::quat PreviousCameraRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            uint32_t ShadowFreezeFramesRemaining = 0;
        };

        Lighting2DRendererState g_State{};

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

        float EstimatePixelsPerWorldUnit(const glm::mat4& viewProjection, uint32_t width, uint32_t height)
        {
            // Analytically derive the screen-space scale at the viewport
            // center for a unit step along the world X-axis.  Using only
            // matrix coefficients avoids the discontinuity that projection-
            // based estimation caused: when test points crossed the near
            // plane during camera rotation, pixelsPerUnit jumped between the
            // projected value and the analytical fallback, making shadow
            // bias/distance/softness flash between frames.
            //
            // For a perspective VP matrix at the Z=0 scene plane, the world
            // X-axis (1,0,0,0) maps to VP column 0 in clip space.  The
            // screen-space length of that vector is:
            //   sqrt(VP[0][0]^2 + VP[0][1]^2) / |VP[3][3]| * halfWidth
            // Using both clip X and Y handles camera rotations that project
            // the world X-axis partially into the screen Y direction.
            const float vpW = std::abs(viewProjection[3][3]);
            if (vpW > kEpsilon)
            {
                const float clipLenXY = std::sqrt(
                    viewProjection[0][0] * viewProjection[0][0] +
                    viewProjection[0][1] * viewProjection[0][1]);
                const float estimate = (clipLenXY / vpW) * static_cast<float>(width) * 0.5f;
                const float maxReasonable = static_cast<float>(std::max(width, height)) * 2.0f;
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
            shaderAsset = Assets::AssetManager::LoadBlocking<Assets::ShaderAsset>(shaderKey);
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
            if (!g_State.WhiteTexture)
            {
                const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
                TextureSpecification whiteSpecification{};
                whiteSpecification.GenerateMipmaps = false;
                whiteSpecification.MinFilter = TextureFilter::Nearest;
                whiteSpecification.MagFilter = TextureFilter::Nearest;
                whiteSpecification.WrapU = TextureWrap::ClampToEdge;
                whiteSpecification.WrapV = TextureWrap::ClampToEdge;
                g_State.WhiteTexture = Texture2D::CreateFromRGBA8(1, 1, whitePixel, whiteSpecification);
            }

            if (!g_State.FlatNormalTexture)
            {
                const uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };
                TextureSpecification normalSpecification{};
                normalSpecification.GenerateMipmaps = false;
                normalSpecification.MinFilter = TextureFilter::Nearest;
                normalSpecification.MagFilter = TextureFilter::Nearest;
                normalSpecification.WrapU = TextureWrap::ClampToEdge;
                normalSpecification.WrapV = TextureWrap::ClampToEdge;
                g_State.FlatNormalTexture = Texture2D::CreateFromRGBA8(1, 1, flatNormalPixel, normalSpecification);
            }
        }

        void EnsureQuadGeometryCreated()
        {
            if (g_State.UnitQuadVertexArray && g_State.UnitQuadVertexBuffer)
                return;

            const std::array<float, 16> quadVertices = {
                -0.5f, -0.5f, 0.0f, 0.0f,
                 0.5f, -0.5f, 1.0f, 0.0f,
                -0.5f,  0.5f, 0.0f, 1.0f,
                 0.5f,  0.5f, 1.0f, 1.0f
            };

            g_State.UnitQuadVertexArray = VertexArray::Create();
            g_State.UnitQuadVertexBuffer = VertexBuffer::Create(quadVertices.data(), static_cast<uint32_t>(sizeof(quadVertices)));
            g_State.UnitQuadVertexBuffer->SetLayout({
                { ShaderDataType::Float2, "a_Position" },
                { ShaderDataType::Float2, "a_UV" }
            });
            g_State.UnitQuadVertexArray->AddVertexBuffer(g_State.UnitQuadVertexBuffer);
        }

        bool EnsureFramebuffers(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return false;

            if (!g_State.GBufferFramebuffer)
            {
                FramebufferSpecification gBufferSpec{};
                gBufferSpec.Width = width;
                gBufferSpec.Height = height;
                gBufferSpec.Samples = 1;
                gBufferSpec.ColorAttachmentCount = 2;
                gBufferSpec.DepthAttachment = true;
                gBufferSpec.StencilAttachment = false;
                gBufferSpec.SwapChainTarget = false;
                g_State.GBufferFramebuffer = Framebuffer::Create(gBufferSpec);
            }

            if (!g_State.LightFramebuffer)
            {
                FramebufferSpecification lightSpec{};
                lightSpec.Width = width;
                lightSpec.Height = height;
                lightSpec.Samples = 1;
                lightSpec.ColorAttachmentCount = 1;
                lightSpec.DepthAttachment = false;
                lightSpec.StencilAttachment = false;
                lightSpec.SwapChainTarget = false;
                g_State.LightFramebuffer = Framebuffer::Create(lightSpec);
            }

            if (!g_State.GBufferFramebuffer || !g_State.LightFramebuffer)
                return false;

            if (g_State.GBufferFramebuffer->GetWidth() != width || g_State.GBufferFramebuffer->GetHeight() != height)
                g_State.GBufferFramebuffer->Resize(width, height);
            if (g_State.LightFramebuffer->GetWidth() != width || g_State.LightFramebuffer->GetHeight() != height)
                g_State.LightFramebuffer->Resize(width, height);

            g_State.FramebufferWidth = width;
            g_State.FramebufferHeight = height;
            return true;
        }

        std::vector<entt::entity> BuildSortedSpriteRenderList(Scene& scene, float interpolationAlpha)
        {
            auto& registry = scene.GetRegistry();
            auto view = registry.view<TransformComponent, SpriteComponent>();

            std::vector<entt::entity> entities;
            entities.reserve(view.size_hint());
            for (entt::entity entity : view)
            {
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                entities.push_back(entity);
            }

            std::sort(entities.begin(), entities.end(), [&scene, &registry, interpolationAlpha](entt::entity left, entt::entity right) {
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

            if (!material.CachedMaterial && !material.MaterialLoadAttempted)
            {
                material.CachedMaterial = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(material.MaterialKey);
                material.MaterialLoadAttempted = true;
            }
            else if (!material.CachedMaterial && material.MaterialLoadAttempted)
            {
                material.CachedMaterial = std::dynamic_pointer_cast<Assets::MaterialAsset>(
                    Assets::AssetManager::GetCachedByKey(material.MaterialKey));
            }
        }

        void RefreshSpriteTextureCache(SpriteComponent& sprite)
        {
            if (sprite.TextureKey.empty())
                return;

            if (!sprite.CachedTexture && !sprite.TextureLoadAttempted)
            {
                sprite.CachedTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(sprite.TextureKey);
                sprite.TextureLoadAttempted = true;
            }
            else if (!sprite.CachedTexture && sprite.TextureLoadAttempted)
            {
                sprite.CachedTexture = std::dynamic_pointer_cast<Assets::TextureAsset>(
                    Assets::AssetManager::GetCachedByKey(sprite.TextureKey));
            }
        }

        std::vector<NormalPassSpriteDraw> BuildNormalPassDrawList(Scene& scene, float interpolationAlpha)
        {
            std::vector<NormalPassSpriteDraw> drawList;
            auto sortedEntities = BuildSortedSpriteRenderList(scene, interpolationAlpha);
            drawList.reserve(sortedEntities.size());

            auto& registry = scene.GetRegistry();
            for (entt::entity entity : sortedEntities)
            {
                auto& sprite = registry.get<SpriteComponent>(entity);

                NormalPassSpriteDraw draw{};
                draw.Model = scene.GetWorldTransformMatrixForRendering(entity, interpolationAlpha);
                draw.Color = sprite.Color;
                draw.AlbedoTexture = g_State.WhiteTexture;
                draw.NormalTexture = g_State.FlatNormalTexture;
                draw.NormalStrength = 1.0f;
                draw.ReceiveShadows = sprite.ReceiveShadows;

                bool hasMaterialButFailed = false;
                if (auto* material = registry.try_get<MaterialComponent>(entity))
                {
                    RefreshSpriteMaterialCache(*material);
                    if (material->CachedMaterial)
                    {
                        if (auto mainTexture = material->CachedMaterial->GetMainTexture())
                            draw.AlbedoTexture = mainTexture;

                        if (g_State.Settings.EnableNormalMaps)
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

                if (!hasMaterialButFailed && draw.AlbedoTexture == g_State.WhiteTexture && !sprite.TextureKey.empty())
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

        void SubmitSelectGBufferDrawBuffers()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([](GraphicsContext*) {
                const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                glDrawBuffers(2, drawBuffers);
            }, "Lighting2D/SetGBufferDrawBuffers"));
        }

        void SubmitSelectAlbedoAttachmentOnly()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([](GraphicsContext*) {
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
            }, "Lighting2D/SetAlbedoAttachmentOnly"));
        }

        void SubmitSelectNormalAttachmentOnly()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([](GraphicsContext*) {
                glDrawBuffer(GL_COLOR_ATTACHMENT1);
            }, "Lighting2D/SetNormalAttachmentOnly"));
        }

        void SubmitClearNormalAttachment()
        {
            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([](GraphicsContext*) {
                // Alpha is the receive-shadows mask.
                // Default to 1 so scene pixels receive shadows unless explicitly opted out.
                const GLfloat flatNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
                glClearBufferfv(GL_COLOR, 1, flatNormal);
            }, "Lighting2D/ClearNormalAttachment"));
        }

        void SubmitNormalPassDraws(const std::vector<NormalPassSpriteDraw>& drawList, const glm::mat4& viewProjection)
        {
            if (drawList.empty())
                return;

            auto shader = ResolveShaderFromAsset(g_State.GBufferNormalShaderAsset, kGBufferNormalShaderKey);
            if (!shader || !g_State.UnitQuadVertexArray)
                return;

            Renderer::GetInstance().SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            Renderer::GetInstance().SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::Zero, false));
            SubmitSelectNormalAttachmentOnly();
            const float shadowAlphaCutoff = std::clamp(g_State.Settings.ShadowAlphaCutoff, 0.0f, 1.0f);

            for (const NormalPassSpriteDraw& draw : drawList)
            {
                auto shaderRef = shader;
                auto vertexArrayRef = g_State.UnitQuadVertexArray;
                auto albedoRef = draw.AlbedoTexture ? draw.AlbedoTexture : g_State.WhiteTexture;
                auto normalRef = draw.NormalTexture ? draw.NormalTexture : g_State.FlatNormalTexture;
                const glm::mat4 model = draw.Model;
                const glm::vec4 color = draw.Color;
                const float normalStrength = draw.NormalStrength;
                const int receiveShadows = draw.ReceiveShadows ? 1 : 0;
                Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([shaderRef, vertexArrayRef, albedoRef, normalRef, viewProjection, model, color, normalStrength, receiveShadows, shadowAlphaCutoff](GraphicsContext*) {
                    auto* glShader = dynamic_cast<OpenGLShader*>(shaderRef.get());
                    auto* glVertexArray = dynamic_cast<OpenGLVertexArray*>(vertexArrayRef.get());
                    auto* glAlbedoTexture = dynamic_cast<OpenGLTexture2D*>(albedoRef.get());
                    auto* glNormalTexture = dynamic_cast<OpenGLTexture2D*>(normalRef.get());
                    if (!glShader || !glVertexArray || !glAlbedoTexture || !glNormalTexture)
                        return;

                    const GLuint shaderProgram = glShader->GetRendererID();
                    glUseProgram(shaderProgram);
                    glBindVertexArray(glVertexArray->GetRendererID());
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, glAlbedoTexture->GetRendererID());
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, glNormalTexture->GetRendererID());

                    if (GLint location = glGetUniformLocation(shaderProgram, "u_ViewProjection"); location != -1)
                        glUniformMatrix4fv(location, 1, GL_FALSE, &viewProjection[0][0]);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_Model"); location != -1)
                        glUniformMatrix4fv(location, 1, GL_FALSE, &model[0][0]);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_Color"); location != -1)
                        glUniform4f(location, color.r, color.g, color.b, color.a);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_NormalStrength"); location != -1)
                        glUniform1f(location, normalStrength);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_ReceiveShadows"); location != -1)
                        glUniform1i(location, receiveShadows);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_ShadowAlphaCutoff"); location != -1)
                        glUniform1f(location, shadowAlphaCutoff);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_AlbedoTexture"); location != -1)
                        glUniform1i(location, 0);
                    if (GLint location = glGetUniformLocation(shaderProgram, "u_NormalTexture"); location != -1)
                        glUniform1i(location, 1);

                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }, "Lighting2D/NormalPassSprite"));
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

        std::vector<glm::vec4> BuildShadowSegments(Scene& scene,
                                                   float interpolationAlpha,
                                                   const glm::mat4& viewProjection,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t maxSegments,
                                                   uint32_t& outOccluderCount,
                                                   float shadowSegmentSnapPixels)
        {
            std::vector<glm::vec4> segments;
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

                    segments.emplace_back(screenA.x, screenA.y, screenB.x, screenB.y);
                };

                for (size_t index = 0; index + 1 < clipPoints.size(); ++index)
                    addClippedEdge(clipPoints[index], clipPoints[index + 1]);

                if (closed && clipPoints.size() >= 3 && segments.size() < maxSegments)
                    addClippedEdge(clipPoints.back(), clipPoints.front());
            };

            auto occluderView = registry.view<ShadowOccluder2DComponent>();
            for (entt::entity entity : occluderView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (!scene.IsEntityEnabledInHierarchy(entity))
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

            // Hybrid fallback: collider-backed occluders for entities without an explicit ShadowOccluder2D.
            // This keeps scene authoring fast while preserving explicit component control when present.
            auto boxColliderView = registry.view<BoxCollider2DComponent>();
            for (entt::entity entity : boxColliderView)
            {
                if (segments.size() >= maxSegments)
                    break;
                if (explicitOccluderEntities.contains(entity))
                    continue;
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (const auto* sprite = registry.try_get<SpriteComponent>(entity); sprite && !sprite->CastShadows)
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
                if (!scene.IsEntityEnabledInHierarchy(entity))
                    continue;
                if (const auto* sprite = registry.try_get<SpriteComponent>(entity); sprite && !sprite->CastShadows)
                    continue;

                std::vector<glm::vec2> localPoints;
                BuildPhysicsOccluderPolygon(registry, entity, localPoints);
                appendOccluderSegments(entity, localPoints, true);
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
                                                                   float pixelsPerUnit)
        {
            std::vector<ScreenDirectionalLight> lights;
            lights.reserve(std::max(0, g_State.Settings.MaxDirectionalLights));

            const uint32_t maxDirectionalLights = ClampDirectionalLightsByQuality(g_State.Settings);
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
                screenLight.Direction = screenDirection;
                screenLight.CastShadows = g_State.Settings.EnableShadows && directional.CastShadows;
                screenLight.ShadowStrength = std::clamp(directional.ShadowStrength, 0.0f, 1.0f);
                screenLight.ShadowSoftnessPixels = std::max(0.0f, directional.ShadowSoftness * g_State.Settings.ShadowSoftnessScale * pixelsPerUnit);
                screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State.Settings, directional.ShadowSamples);
                screenLight.ShadowDistancePixels = std::max(1.0f, directional.ShadowDistance * pixelsPerUnit);
                // Screen-space directional ray tests need a bias to avoid self-occlusion on receiver quads.
                const float biasScale = std::max(0.0f, g_State.Settings.DirectionalShadowBiasScale);
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
                                                       float pixelsPerUnit)
        {
            std::vector<ScreenPointLight> lights;
            lights.reserve(std::max(0, g_State.Settings.MaxPointLights));

            const uint32_t maxPointLights = ClampPointLightsByQuality(g_State.Settings);
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
                screenLight.CastShadows = g_State.Settings.EnableShadows && pointLight.CastShadows;
                screenLight.ShadowStrength = std::clamp(pointLight.ShadowStrength, 0.0f, 1.0f);
                screenLight.ShadowSoftnessPixels = std::max(0.0f, pointLight.ShadowSoftness * g_State.Settings.ShadowSoftnessScale * pixelsPerUnit);
                screenLight.ShadowSamples = ClampShadowSamplesByQuality(g_State.Settings, pointLight.ShadowSamples);
                screenLight.ShadowBiasPixels = std::max(0.0f, pointLight.ShadowBias * pixelsPerUnit);
                lights.push_back(screenLight);
            }

            return lights;
        }

        void SubmitDirectionalLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                                        const std::shared_ptr<Texture2D>& normalTexture,
                                        const std::vector<glm::vec4>& shadowSegments,
                                        const ScreenDirectionalLight& light,
                                        uint32_t width,
                                        uint32_t height,
                                        float shadowSegmentSnapPixels)
        {
            auto shader = ResolveShaderFromAsset(g_State.DirectionalLightShaderAsset, kDirectionalLightShaderKey);
            if (!shader || !g_State.UnitQuadVertexArray)
                return;

            auto shaderRef = shader;
            auto vertexArrayRef = g_State.UnitQuadVertexArray;
            auto albedoRef = albedoTexture;
            auto normalRef = normalTexture;
            const glm::vec3 lightColor = light.Color;
            const float intensity = light.Intensity;
            const glm::vec2 lightDirection = light.Direction;
            const int useShadows = light.CastShadows ? 1 : 0;
            const float shadowStrength = light.ShadowStrength;
            const float shadowSoftness = light.ShadowSoftnessPixels;
            const int shadowSamples = light.ShadowSamples;
            const float shadowDistance = light.ShadowDistancePixels;
            const float shadowBias = light.ShadowBiasPixels;
            const float shadowAlphaCutoff = std::clamp(g_State.Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
            const float shadowSegmentSnapPixelsClamped = std::max(0.0f, shadowSegmentSnapPixels);
            // Use all shadow segments without screen-space facing filter.
            // The ray-segment intersection in the shader is geometrically
            // correct regardless of edge orientation, so the filter was purely
            // a performance optimization.  Under perspective camera rotation
            // the screen-space edge normals shift rapidly, causing the filter
            // to pop edges in/out and produce shadow flicker.
            const std::vector<glm::vec4>& segments = shadowSegments;

            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([shaderRef, vertexArrayRef, albedoRef, normalRef, lightColor, intensity, lightDirection, useShadows, shadowStrength, shadowSoftness, shadowSamples, shadowDistance, shadowBias, shadowAlphaCutoff, shadowSegmentSnapPixelsClamped, width, height, segments = std::move(segments)](GraphicsContext*) {
                auto* glShader = dynamic_cast<OpenGLShader*>(shaderRef.get());
                auto* glVertexArray = dynamic_cast<OpenGLVertexArray*>(vertexArrayRef.get());
                auto* glAlbedoTexture = dynamic_cast<OpenGLTexture2D*>(albedoRef.get());
                auto* glNormalTexture = dynamic_cast<OpenGLTexture2D*>(normalRef.get());
                if (!glShader || !glVertexArray || !glAlbedoTexture || !glNormalTexture)
                    return;

                const GLuint program = glShader->GetRendererID();
                glUseProgram(program);
                glBindVertexArray(glVertexArray->GetRendererID());

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, glAlbedoTexture->GetRendererID());
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, glNormalTexture->GetRendererID());

                if (GLint location = glGetUniformLocation(program, "u_AlbedoTexture"); location != -1)
                    glUniform1i(location, 0);
                if (GLint location = glGetUniformLocation(program, "u_NormalTexture"); location != -1)
                    glUniform1i(location, 1);
                if (GLint location = glGetUniformLocation(program, "u_ViewportSize"); location != -1)
                    glUniform2f(location, static_cast<float>(width), static_cast<float>(height));
                if (GLint location = glGetUniformLocation(program, "u_LightColor"); location != -1)
                    glUniform3f(location, lightColor.r, lightColor.g, lightColor.b);
                if (GLint location = glGetUniformLocation(program, "u_LightIntensity"); location != -1)
                    glUniform1f(location, intensity);
                if (GLint location = glGetUniformLocation(program, "u_LightDirection"); location != -1)
                    glUniform2f(location, lightDirection.x, lightDirection.y);
                if (GLint location = glGetUniformLocation(program, "u_UseShadows"); location != -1)
                    glUniform1i(location, useShadows);
                if (GLint location = glGetUniformLocation(program, "u_ShadowStrength"); location != -1)
                    glUniform1f(location, shadowStrength);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSoftness"); location != -1)
                    glUniform1f(location, shadowSoftness);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSamples"); location != -1)
                    glUniform1i(location, shadowSamples);
                if (GLint location = glGetUniformLocation(program, "u_ShadowDistance"); location != -1)
                    glUniform1f(location, shadowDistance);
                if (GLint location = glGetUniformLocation(program, "u_ShadowBias"); location != -1)
                    glUniform1f(location, shadowBias);
                if (GLint location = glGetUniformLocation(program, "u_ShadowAlphaCutoff"); location != -1)
                    glUniform1f(location, shadowAlphaCutoff);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSegmentSnapPixels"); location != -1)
                    glUniform1f(location, shadowSegmentSnapPixelsClamped);

                const int segmentCount = static_cast<int>(std::min<size_t>(segments.size(), kShaderShadowSegmentCap));
                if (GLint location = glGetUniformLocation(program, "u_ShadowSegmentCount"); location != -1)
                    glUniform1i(location, segmentCount);
                if (segmentCount > 0)
                {
                    if (GLint location = glGetUniformLocation(program, "u_ShadowSegments"); location != -1)
                        glUniform4fv(location, segmentCount, &segments[0][0]);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }, "Lighting2D/DirectionalLightPass"));
        }

        void SubmitPointLightPass(const std::shared_ptr<Texture2D>& albedoTexture,
                                  const std::shared_ptr<Texture2D>& normalTexture,
                                  const std::vector<glm::vec4>& shadowSegments,
                                  const ScreenPointLight& light,
                                  uint32_t width,
                                  uint32_t height,
                                  float shadowSegmentSnapPixels)
        {
            auto shader = ResolveShaderFromAsset(g_State.PointLightShaderAsset, kPointLightShaderKey);
            if (!shader || !g_State.UnitQuadVertexArray)
                return;

            auto shaderRef = shader;
            auto vertexArrayRef = g_State.UnitQuadVertexArray;
            auto albedoRef = albedoTexture;
            auto normalRef = normalTexture;
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
            const float shadowAlphaCutoff = std::clamp(g_State.Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
            const float shadowSegmentSnapPixelsClamped = std::max(0.0f, shadowSegmentSnapPixels);
            std::vector<glm::vec4> segments = shadowSegments;

            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([shaderRef, vertexArrayRef, albedoRef, normalRef, lightColor, intensity, lightPosition, lightRadius, lightFalloff, useShadows, shadowStrength, shadowSoftness, shadowSamples, shadowBias, shadowAlphaCutoff, shadowSegmentSnapPixelsClamped, width, height, segments = std::move(segments)](GraphicsContext*) {
                auto* glShader = dynamic_cast<OpenGLShader*>(shaderRef.get());
                auto* glVertexArray = dynamic_cast<OpenGLVertexArray*>(vertexArrayRef.get());
                auto* glAlbedoTexture = dynamic_cast<OpenGLTexture2D*>(albedoRef.get());
                auto* glNormalTexture = dynamic_cast<OpenGLTexture2D*>(normalRef.get());
                if (!glShader || !glVertexArray || !glAlbedoTexture || !glNormalTexture)
                    return;

                const GLuint program = glShader->GetRendererID();
                glUseProgram(program);
                glBindVertexArray(glVertexArray->GetRendererID());

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, glAlbedoTexture->GetRendererID());
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, glNormalTexture->GetRendererID());

                if (GLint location = glGetUniformLocation(program, "u_AlbedoTexture"); location != -1)
                    glUniform1i(location, 0);
                if (GLint location = glGetUniformLocation(program, "u_NormalTexture"); location != -1)
                    glUniform1i(location, 1);
                if (GLint location = glGetUniformLocation(program, "u_ViewportSize"); location != -1)
                    glUniform2f(location, static_cast<float>(width), static_cast<float>(height));
                if (GLint location = glGetUniformLocation(program, "u_LightColor"); location != -1)
                    glUniform3f(location, lightColor.r, lightColor.g, lightColor.b);
                if (GLint location = glGetUniformLocation(program, "u_LightIntensity"); location != -1)
                    glUniform1f(location, intensity);
                if (GLint location = glGetUniformLocation(program, "u_LightPosition"); location != -1)
                    glUniform2f(location, lightPosition.x, lightPosition.y);
                if (GLint location = glGetUniformLocation(program, "u_LightRadius"); location != -1)
                    glUniform1f(location, lightRadius);
                if (GLint location = glGetUniformLocation(program, "u_LightFalloff"); location != -1)
                    glUniform1f(location, lightFalloff);
                if (GLint location = glGetUniformLocation(program, "u_UseShadows"); location != -1)
                    glUniform1i(location, useShadows);
                if (GLint location = glGetUniformLocation(program, "u_ShadowStrength"); location != -1)
                    glUniform1f(location, shadowStrength);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSoftness"); location != -1)
                    glUniform1f(location, shadowSoftness);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSamples"); location != -1)
                    glUniform1i(location, shadowSamples);
                if (GLint location = glGetUniformLocation(program, "u_ShadowBias"); location != -1)
                    glUniform1f(location, shadowBias);
                if (GLint location = glGetUniformLocation(program, "u_ShadowAlphaCutoff"); location != -1)
                    glUniform1f(location, shadowAlphaCutoff);
                if (GLint location = glGetUniformLocation(program, "u_ShadowSegmentSnapPixels"); location != -1)
                    glUniform1f(location, shadowSegmentSnapPixelsClamped);

                const int segmentCount = static_cast<int>(std::min<size_t>(segments.size(), kShaderShadowSegmentCap));
                if (GLint location = glGetUniformLocation(program, "u_ShadowSegmentCount"); location != -1)
                    glUniform1i(location, segmentCount);
                if (segmentCount > 0)
                {
                    if (GLint location = glGetUniformLocation(program, "u_ShadowSegments"); location != -1)
                        glUniform4fv(location, segmentCount, &segments[0][0]);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }, "Lighting2D/PointLightPass"));
        }

        void SubmitCompositePass(const std::shared_ptr<Texture2D>& albedoTexture,
                                 const std::shared_ptr<Texture2D>& lightTexture)
        {
            auto shader = ResolveShaderFromAsset(g_State.CompositeShaderAsset, kCompositeShaderKey);
            if (!shader || !g_State.UnitQuadVertexArray || !albedoTexture || !lightTexture)
                return;

            auto shaderRef = shader;
            auto vertexArrayRef = g_State.UnitQuadVertexArray;
            auto albedoRef = albedoTexture;
            auto lightRef = lightTexture;
            Renderer::GetInstance().SubmitCommand(std::make_unique<CustomCommand>([shaderRef, vertexArrayRef, albedoRef, lightRef](GraphicsContext*) {
                auto* glShader = dynamic_cast<OpenGLShader*>(shaderRef.get());
                auto* glVertexArray = dynamic_cast<OpenGLVertexArray*>(vertexArrayRef.get());
                auto* glAlbedoTexture = dynamic_cast<OpenGLTexture2D*>(albedoRef.get());
                auto* glLightTexture = dynamic_cast<OpenGLTexture2D*>(lightRef.get());
                if (!glShader || !glVertexArray || !glAlbedoTexture || !glLightTexture)
                    return;

                const GLuint program = glShader->GetRendererID();
                glUseProgram(program);
                glBindVertexArray(glVertexArray->GetRendererID());

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, glAlbedoTexture->GetRendererID());
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, glLightTexture->GetRendererID());

                if (GLint location = glGetUniformLocation(program, "u_AlbedoTexture"); location != -1)
                    glUniform1i(location, 0);
                if (GLint location = glGetUniformLocation(program, "u_LightTexture"); location != -1)
                    glUniform1i(location, 1);

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }, "Lighting2D/CompositePass"));
        }

        bool PrepareResources(uint32_t width, uint32_t height)
        {
            EnsureFallbackTextures();
            EnsureQuadGeometryCreated();
            if (!EnsureFramebuffers(width, height))
                return false;

            if (!ResolveShaderFromAsset(g_State.GBufferNormalShaderAsset, kGBufferNormalShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State.DirectionalLightShaderAsset, kDirectionalLightShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State.PointLightShaderAsset, kPointLightShaderKey))
                return false;
            if (!ResolveShaderFromAsset(g_State.CompositeShaderAsset, kCompositeShaderKey))
                return false;

            return g_State.GBufferFramebuffer &&
                   g_State.LightFramebuffer &&
                   g_State.UnitQuadVertexArray &&
                   g_State.WhiteTexture &&
                   g_State.FlatNormalTexture;
        }
    }

    namespace Lighting2DRenderer
    {
        void SetSettings(const Lighting2DSettings& settings)
        {
            g_State.Settings = settings;
            g_State.Settings.ShadowQualityLevel = std::clamp(g_State.Settings.ShadowQualityLevel, 0, 2);
            g_State.Settings.MaxDirectionalLights = std::max(0, g_State.Settings.MaxDirectionalLights);
            g_State.Settings.MaxPointLights = std::max(0, g_State.Settings.MaxPointLights);
            g_State.Settings.MaxShadowSegments = std::max(1, g_State.Settings.MaxShadowSegments);
            g_State.Settings.MaxShadowSamplesPerLight = std::max(1, g_State.Settings.MaxShadowSamplesPerLight);
            g_State.Settings.AmbientIntensity = std::max(0.0f, g_State.Settings.AmbientIntensity);
            g_State.Settings.ShadowSoftnessScale = std::max(0.0f, g_State.Settings.ShadowSoftnessScale);
            g_State.Settings.DirectionalShadowBiasScale = std::max(0.0f, g_State.Settings.DirectionalShadowBiasScale);
            g_State.Settings.ShadowAlphaCutoff = std::clamp(g_State.Settings.ShadowAlphaCutoff, 0.0f, 1.0f);
            g_State.Settings.ShadowSegmentSnapPixels = std::max(0.0f, g_State.Settings.ShadowSegmentSnapPixels);
            g_State.Settings.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, g_State.Settings.ShadowFreezeAngularVelocityDegreesPerSecond);
            g_State.Settings.ShadowFreezeFrameCount = std::max(1, g_State.Settings.ShadowFreezeFrameCount);
            if (!g_State.Settings.EnableHighAngularVelocityShadowFreeze)
                g_State.ShadowFreezeFramesRemaining = 0;
        }

        const Lighting2DSettings& GetSettings()
        {
            return g_State.Settings;
        }

        const Lighting2DDiagnostics& GetDiagnostics()
        {
            return g_State.Diagnostics;
        }

        bool RenderToViewport(Scene& scene,
                              const Camera& camera,
                              const std::shared_ptr<Framebuffer>& targetFramebuffer,
                              uint32_t width,
                              uint32_t height,
                              const std::function<void()>& renderWorldAlbedoPass)
        {
            g_State.Diagnostics = {};

            if (!g_State.Settings.Enabled || width == 0 || height == 0)
                return false;

            auto& renderer = Renderer::GetInstance();
            if (!renderer.IsInitialized())
                return false;

            const auto buildStart = std::chrono::high_resolution_clock::now();
            if (!PrepareResources(width, height))
                return false;

            const float interpolationAlpha = ComputeInterpolationAlpha();
            const glm::mat4 viewProjection = camera.GetViewProjectionMatrix();
            const float pixelsPerUnit = EstimatePixelsPerWorldUnit(viewProjection, width, height);

            const glm::mat4 cameraViewMatrix = camera.GetViewMatrix();
            std::vector<NormalPassSpriteDraw> normalPassDraws = BuildNormalPassDrawList(scene, interpolationAlpha);
            uint32_t occluderCount = 0;
            const uint32_t maxShadowSegments = ClampSegmentsByQuality(g_State.Settings);
            float effectiveShadowSegmentSnapPixels = std::max(0.0f, g_State.Settings.ShadowSegmentSnapPixels);
            if (camera.GetUsage() == CameraUsage::Editor)
            {
                // Disable screen-space snapping for the editor camera.
                // Snapping rounds segment endpoints to a fixed screen-space grid,
                // which helps prevent sub-pixel shimmer on a STATIC camera.
                // But when the editor camera rotates, the world moves relative to
                // the fixed grid, causing endpoints to jump between grid positions
                // every frame.  This produces visible shadow edge flicker that is
                // far more distracting than the sub-pixel shimmer it prevents.
                effectiveShadowSegmentSnapPixels = 0.0f;
            }
            // Shadow freeze during fast camera rotation prevents temporal
            // aliasing of the screen-space shadow system.  Freezing reuses
            // cached segments from the last static frame.  Only enabled for
            // gameplay cameras -- the editor camera updates segments every
            // frame so the light direction and segments are always consistent.
            // Freezing the editor camera caused shadow position mismatch
            // (frozen segments + live direction) and brightness flashes when
            // the freeze released.
            const bool allowAngularVelocityShadowFreeze =
                g_State.Settings.EnableHighAngularVelocityShadowFreeze && camera.GetUsage() == CameraUsage::Gameplay;
            const glm::quat currentCameraRotation = ExtractCameraRotationFromViewMatrix(cameraViewMatrix);
            float cameraAngularVelocityDegreesPerSecond = 0.0f;
            if (allowAngularVelocityShadowFreeze && g_State.HasPreviousCameraRotation)
            {
                const float deltaTimeSeconds = std::max(Time::GetUnscaledDeltaTimeSeconds(), kEpsilon);
                cameraAngularVelocityDegreesPerSecond = ComputeAngularVelocityDegreesPerSecond(
                    g_State.PreviousCameraRotation,
                    currentCameraRotation,
                    deltaTimeSeconds);
            }
            if (allowAngularVelocityShadowFreeze)
            {
                g_State.HasPreviousCameraRotation = true;
                g_State.PreviousCameraRotation = currentCameraRotation;
            }
            else
            {
                g_State.HasPreviousCameraRotation = false;
                g_State.ShadowFreezeFramesRemaining = 0;
            }

            const float freezeThreshold = g_State.Settings.ShadowFreezeAngularVelocityDegreesPerSecond;
            const int freezeFrameCount = g_State.Settings.ShadowFreezeFrameCount;

            if (allowAngularVelocityShadowFreeze &&
                cameraAngularVelocityDegreesPerSecond >= freezeThreshold)
            {
                const uint32_t requestedFreezeFrames = static_cast<uint32_t>(std::max(1, freezeFrameCount));
                g_State.ShadowFreezeFramesRemaining = std::max(g_State.ShadowFreezeFramesRemaining, requestedFreezeFrames);
            }

            std::vector<glm::vec4> shadowSegments;
            const bool useFrozenShadowSegments = allowAngularVelocityShadowFreeze &&
                g_State.ShadowFreezeFramesRemaining > 0 &&
                !g_State.CachedShadowSegments.empty();
            if (useFrozenShadowSegments)
            {
                shadowSegments = g_State.CachedShadowSegments;
                occluderCount = g_State.CachedShadowOccluderCount;
            }
            else
            {
                shadowSegments = BuildShadowSegments(scene, interpolationAlpha, viewProjection, width, height, maxShadowSegments, occluderCount, effectiveShadowSegmentSnapPixels);
                g_State.CachedShadowSegments = shadowSegments;
                g_State.CachedShadowOccluderCount = occluderCount;
            }
            if (allowAngularVelocityShadowFreeze && g_State.ShadowFreezeFramesRemaining > 0)
                --g_State.ShadowFreezeFramesRemaining;
            std::vector<ScreenDirectionalLight> directionalLights = BuildDirectionalLights(scene, interpolationAlpha, camera, viewProjection, width, height, pixelsPerUnit);
            std::vector<ScreenPointLight> pointLights = BuildPointLights(scene, interpolationAlpha, viewProjection, width, height, pixelsPerUnit);

            const auto buildEnd = std::chrono::high_resolution_clock::now();
            g_State.Diagnostics.CpuBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();
            g_State.Diagnostics.ShadowOccluderCount = occluderCount;
            g_State.Diagnostics.ShadowSegmentCount = static_cast<uint32_t>(shadowSegments.size());
            g_State.Diagnostics.DirectionalLightsRendered = static_cast<uint32_t>(directionalLights.size());
            g_State.Diagnostics.PointLightsRendered = static_cast<uint32_t>(pointLights.size());

            const auto submitStart = std::chrono::high_resolution_clock::now();

            // 1) Build GBuffer albedo and normal attachments.
            renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(g_State.GBufferFramebuffer));
            renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));
            SubmitSelectGBufferDrawBuffers();

            ClearCommand::ClearFlags gBufferClearFlags{};
            gBufferClearFlags.color = true;
            gBufferClearFlags.depth = true;
            gBufferClearFlags.stencil = false;
            renderer.SubmitCommand(std::make_unique<ClearCommand>(gBufferClearFlags, 0.0f, 0.0f, 0.0f, 0.0f));
            SubmitClearNormalAttachment();

            // Restrict the albedo pass to attachment 0 only.  The Renderer2D
            // shader has a single output at location 0; leaving attachment 1
            // active produces undefined fragment output on the normal channel
            // per the OpenGL spec, which some drivers handle by corrupting the
            // normal attachment in view-dependent ways (visible as color flashes
            // when rotating the editor camera).
            SubmitSelectAlbedoAttachmentOnly();
            if (renderWorldAlbedoPass)
                renderWorldAlbedoPass();

            SubmitNormalPassDraws(normalPassDraws, viewProjection);

            // 2) Accumulate lights into light buffer.
            renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(g_State.LightFramebuffer));
            renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));

            const glm::vec3 ambient = glm::max(g_State.Settings.AmbientColor * g_State.Settings.AmbientIntensity, glm::vec3(0.0f));
            ClearCommand::ClearFlags lightClearFlags{};
            lightClearFlags.color = true;
            lightClearFlags.depth = false;
            lightClearFlags.stencil = false;
            renderer.SubmitCommand(std::make_unique<ClearCommand>(lightClearFlags, ambient.r, ambient.g, ambient.b, 1.0f));

            renderer.SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::One, true));

            const auto gBufferAlbedo = g_State.GBufferFramebuffer->GetColorAttachment(0);
            const auto gBufferNormal = g_State.GBufferFramebuffer->GetColorAttachment(1);
            for (const ScreenDirectionalLight& directionalLight : directionalLights)
            {
                SubmitDirectionalLightPass(gBufferAlbedo, gBufferNormal, shadowSegments, directionalLight, width, height, effectiveShadowSegmentSnapPixels);
            }
            for (const ScreenPointLight& pointLight : pointLights)
            {
                SubmitPointLightPass(gBufferAlbedo, gBufferNormal, shadowSegments, pointLight, width, height, effectiveShadowSegmentSnapPixels);
            }

            renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::Zero, false));

            // 3) Composite (albedo * accumulated lighting) into target framebuffer.
            // Null targetFramebuffer means default backbuffer.
            renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(targetFramebuffer));
            renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));

            // Keep target clear color in sync with SceneRenderer's configured viewport clear color.
            const glm::vec4 fallbackClearColor = SceneRenderer::GetViewportClearColor();

            ClearCommand::ClearFlags targetClearFlags{};
            targetClearFlags.color = true;
            targetClearFlags.depth = true;
            targetClearFlags.stencil = false;
            renderer.SubmitCommand(std::make_unique<ClearCommand>(
                targetClearFlags,
                fallbackClearColor.r,
                fallbackClearColor.g,
                fallbackClearColor.b,
                fallbackClearColor.a));

            renderer.SubmitCommand(std::make_unique<SetDepthTestCommand>(false));
            renderer.SubmitCommand(std::make_unique<SetCullFaceCommand>(false));
            // The GBuffer albedo was rendered with standard alpha blending over
            // a black-transparent background, which produces premultiplied-alpha
            // output (RGB = color * alpha).  The composite must use premultiplied
            // blending (One, OneMinusSrcAlpha) to avoid squaring the alpha at
            // semi-transparent edges. The old SrcAlpha mode doubled the alpha
            // contribution, amplifying sub-pixel coverage changes into visible
            // per-frame flicker during camera movement.
            renderer.SubmitCommand(std::make_unique<SetBlendModeCommand>(BlendFactor::One, BlendFactor::OneMinusSrcAlpha, true));
            SubmitCompositePass(gBufferAlbedo, g_State.LightFramebuffer->GetColorAttachment(0));

            const auto submitEnd = std::chrono::high_resolution_clock::now();
            g_State.Diagnostics.CpuSubmitTimeMs = std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();
            g_State.Diagnostics.UsingLightingPath = true;
            return true;
        }
    }
}

