#include "OpenGLDevice.h"

#include "Core/Debug/Log.h"
#include "Core/Error.h"
#include "Graphics/DeviceCapabilities.h"
#include "Graphics/Buffer.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderPipeline.h"
#include "Graphics/Renderer.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include "Graphics/VertexArray.h"
#include "Graphics/OpenGL/OpenGLBuffer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include "Graphics/OpenGL/OpenGLFramebuffer.h"
#include "Graphics/OpenGL/OpenGLRenderPipeline.h"
#include "Graphics/OpenGL/OpenGLShader.h"
#include "Graphics/OpenGL/OpenGLSharedContext.h"
#include "Graphics/OpenGL/OpenGLSharedResourceContext.h"
#include "Graphics/ShaderDescriptor.h"
#include "Graphics/ShaderCompilation/ShaderCompiler.h"
#include "Graphics/OpenGL/OpenGLTexture.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"

#if __has_include(<glad/glad.h>)
    #include <glad/glad.h>
#endif

#include <span>
#include <vector>

namespace Limitless
{
    OpenGLDevice::OpenGLDevice() = default;
    OpenGLDevice::~OpenGLDevice() = default;

    // ---- Capabilities -------------------------------------------------------

    GfxDeviceCapabilities OpenGLDevice::GetCapabilities() const
    {
        GfxDeviceCapabilities caps{};
        if (m_Context)
        {
            caps.MaxTextureSlots = m_Context->GetMaxTextureImageUnits();
        }
        caps.SupportsSharedResourceContext = true;
        caps.SupportsMultisample = true;
        caps.SupportsInstancing = true;
        caps.SupportsBaseVertex = true;
        return caps;
    }

    // ---- Context / lifecycle ------------------------------------------------

    GraphicsContext* OpenGLDevice::GetContext() const
    {
        return m_Context;
    }

    void OpenGLDevice::Initialize(void* /*nativeWindow*/)
    {
        // The OpenGL context is created and owned by the Window/platform layer.
        // OpenGLDevice borrows it from Renderer after context creation.
        // This method is a placeholder for any one-time device-level setup.
        m_Initialized = true;
        LT_CORE_INFO("OpenGLDevice initialized");
    }

    void OpenGLDevice::Shutdown()
    {
        m_Context = nullptr;
        m_Initialized = false;
        LT_CORE_INFO("OpenGLDevice shutdown");
    }

    // ---- Resource creation (synchronous) ------------------------------------

    std::shared_ptr<Shader> OpenGLDevice::CreateShaderFromSource(
        const std::string& name,
        const std::string& vertexSource,
        const std::string& fragmentSource)
    {
        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateShader/FromSource", [&](GraphicsContext*) -> std::shared_ptr<Shader> {
            return std::make_shared<OpenGLShader>(name, vertexSource, fragmentSource);
        });
    }

    std::shared_ptr<Shader> OpenGLDevice::CreateShaderFromDescriptor(const ShaderDescriptor& descriptor)
    {
        // For each stage, prefer SPIR-V (transpile to GLSL via SPIRV-Cross).
        // Fall back to NativeSource if SPIR-V is not provided.
        std::string vertexGLSL;
        std::string fragmentGLSL;

        for (const auto& stage : descriptor.Stages)
        {
            std::string* target = nullptr;
            const char* stageName = nullptr;

            switch (stage.Stage)
            {
                case ShaderStage::Vertex:
                    target = &vertexGLSL;
                    stageName = "vertex";
                    break;
                case ShaderStage::Fragment:
                    target = &fragmentGLSL;
                    stageName = "fragment";
                    break;
                default:
                    LT_CORE_WARN("OpenGLDevice::CreateShaderFromDescriptor: unsupported stage in '{}'; skipping", descriptor.DebugName);
                    continue;
            }

            if (!stage.SPIRV.empty())
            {
                // Canonical path: transpile SPIR-V → GLSL
                auto result = ShaderCompilation::TranspileSpirvToGlsl(stage.SPIRV, 330, false, descriptor.DebugName);
                if (result.IsFailure())
                {
                    LT_CORE_ERROR("OpenGLDevice::CreateShaderFromDescriptor: SPIR-V->GLSL transpile failed for {} stage of '{}': {}",
                                  stageName, descriptor.DebugName, result.GetError().GetErrorMessage());
                    return nullptr;
                }
                *target = result.GetValue();
            }
            else if (!stage.NativeSource.empty())
            {
                // Legacy fallback: use raw GLSL directly
                *target = stage.NativeSource;
            }
            else
            {
                LT_CORE_ERROR("OpenGLDevice::CreateShaderFromDescriptor: no SPIR-V or native source for {} stage of '{}'",
                              stageName, descriptor.DebugName);
                return nullptr;
            }
        }

        if (vertexGLSL.empty() || fragmentGLSL.empty())
        {
            LT_CORE_ERROR("OpenGLDevice::CreateShaderFromDescriptor: '{}' requires both vertex and fragment stages", descriptor.DebugName);
            return nullptr;
        }

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateShader/FromDescriptor",
            [name = descriptor.DebugName, vs = std::move(vertexGLSL), fs = std::move(fragmentGLSL)](GraphicsContext*) -> std::shared_ptr<Shader> {
                return std::make_shared<OpenGLShader>(name, vs, fs);
            });
    }

    std::shared_ptr<Texture2D> OpenGLDevice::CreateTexture2DFromFile(
        const std::string& path,
        const TextureSpecification& specification)
    {
        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateTexture2D/FromFile", [&](GraphicsContext*) -> std::shared_ptr<Texture2D> {
            return std::make_shared<OpenGLTexture2D>(path, specification);
        });
    }

    std::shared_ptr<Texture2D> OpenGLDevice::CreateTexture2DFromRGBA8(
        uint32_t width,
        uint32_t height,
        const void* rgbaPixels,
        const TextureSpecification& specification)
    {
        LT_VERIFY(rgbaPixels != nullptr, "OpenGLDevice::CreateTexture2DFromRGBA8: rgbaPixels is null");
        LT_VERIFY(width > 0 && height > 0, "OpenGLDevice::CreateTexture2DFromRGBA8: texture size must be non-zero");

        // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
        std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                        static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));
        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateTexture2D/FromRGBA8", [=](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
            return std::make_shared<OpenGLTexture2D>(width, height, pixelBytes.data(), specification);
        });
    }

    std::shared_ptr<Texture2D> OpenGLDevice::CreateTexture2DFromMipChain(
        const TextureMipLevelRGBA8View* mipLevels,
        uint32_t mipLevelCount,
        const TextureSpecification& specification)
    {
        LT_VERIFY(mipLevels != nullptr && mipLevelCount > 0, "OpenGLDevice::CreateTexture2DFromMipChain: empty mip chain");

        // Copy pixels for safety
        struct CopiedMip
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            size_t Offset = 0;
        };

        std::vector<CopiedMip> copied;
        copied.reserve(mipLevelCount);

        std::vector<uint8_t> allPixels;
        allPixels.reserve(static_cast<size_t>(mipLevels[0].Width) * static_cast<size_t>(mipLevels[0].Height) * 4u);

        for (uint32_t i = 0; i < mipLevelCount; ++i)
        {
            const auto& mip = mipLevels[i];
            LT_VERIFY(mip.Width > 0 && mip.Height > 0, "OpenGLDevice::CreateTexture2DFromMipChain: invalid mip dimensions");
            LT_VERIFY(mip.PixelsRGBA8 != nullptr, "OpenGLDevice::CreateTexture2DFromMipChain: mip pixels are null");

            const size_t sizeBytes = static_cast<size_t>(mip.Width) * static_cast<size_t>(mip.Height) * 4u;
            const size_t offset = allPixels.size();
            const uint8_t* src = static_cast<const uint8_t*>(mip.PixelsRGBA8);
            allPixels.insert(allPixels.end(), src, src + sizeBytes);
            copied.push_back(CopiedMip{ mip.Width, mip.Height, offset });
        }

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateTexture2D/FromRGBA8MipChain",
            [specification, copied = std::move(copied), pixels = std::move(allPixels)](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
                std::vector<TextureMipLevelRGBA8View> views;
                views.reserve(copied.size());
                for (const auto& m : copied)
                {
                    TextureMipLevelRGBA8View v;
                    v.Width = m.Width;
                    v.Height = m.Height;
                    v.PixelsRGBA8 = pixels.data() + m.Offset;
                    views.push_back(v);
                }
                return std::make_shared<OpenGLTexture2D>(std::span<const TextureMipLevelRGBA8View>(views.data(), views.size()), specification);
            });
    }

    std::shared_ptr<Texture2D> OpenGLDevice::CreateTexture2DForRenderTarget(uint32_t width, uint32_t height)
    {
        LT_VERIFY(width > 0 && height > 0, "OpenGLDevice::CreateTexture2DForRenderTarget: dimensions must be non-zero");

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateTexture2D/ForRenderTarget", [width, height](GraphicsContext*) -> std::shared_ptr<Texture2D> {
            return std::make_shared<OpenGLTexture2D>(width, height);
        });
    }

    std::shared_ptr<Framebuffer> OpenGLDevice::CreateFramebuffer(const FramebufferSpecification& specification)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        LT_VERIFY(specification.ColorAttachmentCount > 0, "Framebuffer must have at least one color attachment");

        auto& renderer = Renderer::GetInstance();
        // FBO-related state must be created on the primary OpenGL context (render thread).
        return renderer.SubmitPrimaryResourceAndWait("CreateFramebuffer", [specification](GraphicsContext*) -> std::shared_ptr<Framebuffer> {
            return std::make_shared<OpenGLFramebuffer>(specification);
        });
    }

    std::shared_ptr<RenderPipeline> OpenGLDevice::CreateRenderPipeline(const RenderPipelineDescriptor& descriptor)
    {
        return std::make_shared<OpenGLRenderPipeline>(descriptor);
    }

    std::shared_ptr<VertexBuffer> OpenGLDevice::CreateVertexBuffer(
        const BufferSpecification& specification,
        const void* initialData)
    {
        LT_VERIFY(specification.Size > 0, "VertexBuffer specification size must be non-zero");

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateVertexBuffer", [&](GraphicsContext*) -> std::shared_ptr<VertexBuffer> {
            return std::make_shared<OpenGLVertexBuffer>(specification, initialData);
        });
    }

    std::shared_ptr<VertexBuffer> OpenGLDevice::CreateVertexBufferRaw(const void* data, uint32_t size)
    {
        return CreateVertexBuffer(BufferSpecification{ size, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, data);
    }

    std::shared_ptr<VertexBuffer> OpenGLDevice::CreateVertexBufferEmpty(uint32_t size)
    {
        return CreateVertexBuffer(BufferSpecification{ size, ResourceUsage::Dynamic, MemoryUsage::CpuToGpu }, nullptr);
    }

    std::shared_ptr<IndexBuffer> OpenGLDevice::CreateIndexBuffer(
        const IndexBufferSpecification& specification,
        const void* indices)
    {
        LT_VERIFY(specification.Count > 0, "IndexBuffer specification count must be non-zero");

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAndWait("CreateIndexBuffer", [&](GraphicsContext*) -> std::shared_ptr<IndexBuffer> {
            return std::make_shared<OpenGLIndexBuffer>(specification, indices);
        });
    }

    std::shared_ptr<IndexBuffer> OpenGLDevice::CreateIndexBufferU32(const uint32_t* indices, uint32_t count)
    {
        return CreateIndexBuffer(IndexBufferSpecification{ count, IndexType::UnsignedInt, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices);
    }

    std::shared_ptr<IndexBuffer> OpenGLDevice::CreateIndexBufferU16(const uint16_t* indices, uint32_t count)
    {
        return CreateIndexBuffer(IndexBufferSpecification{ count, IndexType::UnsignedShort, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices);
    }

    std::shared_ptr<VertexArray> OpenGLDevice::CreateVertexArray()
    {
        auto& renderer = Renderer::GetInstance();
        // VAOs are not shared across OpenGL contexts; creation must happen on the primary context.
        return renderer.SubmitPrimaryResourceAndWait("CreateVertexArray", [&](GraphicsContext*) -> std::shared_ptr<VertexArray> {
            return std::make_shared<OpenGLVertexArray>();
        });
    }

    // ---- Async resource creation --------------------------------------------

    std::future<std::shared_ptr<Texture2D>> OpenGLDevice::CreateTexture2DFromFileAsync(
        const std::string& path,
        const TextureSpecification& specification)
    {
        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAsync("CreateTexture2DAsync/FromFile", [path, specification](GraphicsContext*) -> std::shared_ptr<Texture2D> {
            return std::make_shared<OpenGLTexture2D>(path, specification);
        });
    }

    std::future<std::shared_ptr<Texture2D>> OpenGLDevice::CreateTexture2DFromRGBA8Async(
        uint32_t width,
        uint32_t height,
        const void* rgbaPixels,
        const TextureSpecification& specification)
    {
        LT_VERIFY(rgbaPixels != nullptr, "OpenGLDevice::CreateTexture2DFromRGBA8Async: rgbaPixels is null");
        LT_VERIFY(width > 0 && height > 0, "OpenGLDevice::CreateTexture2DFromRGBA8Async: texture size must be non-zero");

        std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                        static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitResourceAsync("CreateTexture2DAsync/FromRGBA8",
            [width, height, specification, pixels = std::move(pixelBytes)](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
                return std::make_shared<OpenGLTexture2D>(width, height, pixels.data(), specification);
            });
    }

    std::future<std::shared_ptr<Framebuffer>> OpenGLDevice::CreateFramebufferAsync(
        const FramebufferSpecification& specification)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        LT_VERIFY(specification.ColorAttachmentCount > 0, "Framebuffer must have at least one color attachment");

        auto& renderer = Renderer::GetInstance();
        return renderer.SubmitPrimaryResourceAsync("CreateFramebufferAsync",
            [specification](GraphicsContext*) -> std::shared_ptr<Framebuffer> {
                return std::make_shared<OpenGLFramebuffer>(specification);
            });
    }

    // ---- Resource thread support --------------------------------------------

    bool OpenGLDevice::SupportsSharedResourceContext() const
    {
        return m_Context != nullptr;
    }

    std::unique_ptr<SharedResourceContext> OpenGLDevice::CreateSharedResourceContext()
    {
        if (!m_Context)
        {
            LT_CORE_ERROR("OpenGLDevice::CreateSharedResourceContext: no primary context available");
            return nullptr;
        }

        std::unique_ptr<OpenGLSharedContext> shared = m_Context->CreateSharedContext();
        if (!shared)
        {
            LT_CORE_WARN("OpenGLDevice::CreateSharedResourceContext: shared GL context creation failed");
            return nullptr;
        }

        return std::make_unique<OpenGLSharedResourceContext>(std::move(shared));
    }

    // ---- Synchronization ----------------------------------------------------

    void OpenGLDevice::SynchronizeResourceVisibility()
    {
#if __has_include(<glad/glad.h>)
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!fence)
        {
            LT_CORE_WARN("OpenGLDevice: glFenceSync failed; falling back to glFinish");
            glFinish();
            return;
        }

        glFlush();

        for (;;)
        {
            const GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000ull);
            if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
                break;
            if (result == GL_WAIT_FAILED)
            {
                LT_CORE_WARN("OpenGLDevice: glClientWaitSync failed; falling back to glFinish");
                glFinish();
                break;
            }
        }

        glDeleteSync(fence);
#else
        LT_CORE_WARN("OpenGLDevice: OpenGL sync APIs not available; cross-context resource visibility may be unsafe");
#endif
    }

    void OpenGLDevice::WaitIdle()
    {
#if __has_include(<glad/glad.h>)
        glFinish();
#endif
    }

    // ---- Resource retirement ------------------------------------------------

    void OpenGLDevice::RetireNativeResource(const char* debugName,
                                             bool needsPrimaryContext,
                                             std::function<void()> deleteFunc)
    {
        if (!deleteFunc)
            return;

        auto& renderer = Renderer::GetInstance();
        const auto retirementContext = needsPrimaryContext
            ? Renderer::ResourceRetirementContext::Primary
            : Renderer::ResourceRetirementContext::Shared;

        auto deleteFuncForRetirement = deleteFunc;
        const bool retired = renderer.IsInitialized() && renderer.GetGraphicsContext() != nullptr &&
            renderer.RetireResource(debugName, retirementContext,
                [fn = std::move(deleteFuncForRetirement)](GraphicsContext*) { fn(); });

        if (!retired)
        {
            if (m_Context)
            {
                OpenGLContext::ScopedCurrentContext scope(*m_Context);
                deleteFunc();
            }
            else
            {
                LT_CORE_WARN("OpenGLDevice::RetireNativeResource: '{}' lost — no context available", debugName);
            }
        }
    }

    // ---- Thread affinity ----------------------------------------------------

    bool OpenGLDevice::IsOnRenderThread() const
    {
        return Renderer::GetInstance().IsResourceThreadEnabled();
    }

    bool OpenGLDevice::IsCurrentOnThisThread() const
    {
        if (m_Context)
            return m_Context->IsCurrentOnThisThread();
        return false;
    }

    // ---- Factory ------------------------------------------------------------

    std::unique_ptr<GraphicsDevice> GraphicsDevice::Create(GraphicsAPI api)
    {
        switch (api)
        {
            case GraphicsAPI::OpenGL:
                return std::make_unique<OpenGLDevice>();
            case GraphicsAPI::Vulkan:
                LT_CORE_ERROR("GraphicsDevice::Create: Vulkan not yet implemented");
                return nullptr;
            case GraphicsAPI::DirectX:
                LT_CORE_ERROR("GraphicsDevice::Create: DirectX not yet implemented");
                return nullptr;
            case GraphicsAPI::Metal:
                LT_CORE_ERROR("GraphicsDevice::Create: Metal not yet implemented");
                return nullptr;
            default:
                LT_CORE_ERROR("GraphicsDevice::Create: unknown API");
                return nullptr;
        }
    }
}
