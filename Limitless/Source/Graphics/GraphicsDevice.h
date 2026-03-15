#pragma once

#include "Graphics/DeviceCapabilities.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderTypes.h"

#include <functional>
#include <future>
#include <memory>
#include <string>

namespace Limitless
{
    class Framebuffer;
    class RenderPipeline;
    class SharedResourceContext;
    class Shader;
    class Texture2D;
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;

    struct BufferSpecification;
    struct FramebufferSpecification;
    struct IndexBufferSpecification;
    struct RenderPipelineDescriptor;
    struct ShaderDescriptor;
    struct TextureSpecification;
    struct TextureMipLevelRGBA8View;

    /// Backend-neutral graphics device interface.
    ///
    /// Each rendering API (OpenGL, DirectX, Metal, Vulkan) provides a concrete
    /// implementation. Engine-level code interacts exclusively through this
    /// interface; backend headers should never appear outside of their own
    /// implementation files.
    class GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;

        // ---- Identity -------------------------------------------------------

        virtual GraphicsAPI GetAPI() const = 0;

        // ---- Capabilities ---------------------------------------------------

        virtual GfxDeviceCapabilities GetCapabilities() const = 0;

        // ---- Context / lifecycle --------------------------------------------

        /// Returns the underlying GraphicsContext (platform surface, swap chain).
        /// Lifetime is owned by the device.
        virtual GraphicsContext* GetContext() const = 0;

        virtual void Initialize(void* nativeWindow) = 0;
        virtual void Shutdown() = 0;

        // ---- Resource creation (synchronous, render-thread safe) ------------

        virtual std::shared_ptr<Shader> CreateShaderFromSource(
            const std::string& name,
            const std::string& vertexSource,
            const std::string& fragmentSource) = 0;

        /// Create a shader from a ShaderDescriptor (SPIR-V canonical path).
        /// The backend transpiles SPIR-V to its native shading language if needed.
        virtual std::shared_ptr<Shader> CreateShaderFromDescriptor(
            const ShaderDescriptor& descriptor) = 0;

        virtual std::shared_ptr<Texture2D> CreateTexture2DFromFile(
            const std::string& path,
            const TextureSpecification& specification) = 0;

        virtual std::shared_ptr<Texture2D> CreateTexture2DFromRGBA8(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification) = 0;

        virtual std::shared_ptr<Texture2D> CreateTexture2DFromMipChain(
            const TextureMipLevelRGBA8View* mipLevels,
            uint32_t mipLevelCount,
            const TextureSpecification& specification) = 0;

        virtual std::shared_ptr<Texture2D> CreateTexture2DForRenderTarget(
            uint32_t width,
            uint32_t height) = 0;

        virtual std::shared_ptr<Framebuffer> CreateFramebuffer(
            const FramebufferSpecification& specification) = 0;

        virtual std::shared_ptr<RenderPipeline> CreateRenderPipeline(
            const RenderPipelineDescriptor& descriptor) = 0;

        virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(
            const BufferSpecification& specification,
            const void* initialData) = 0;

        virtual std::shared_ptr<VertexBuffer> CreateVertexBufferRaw(
            const void* data,
            uint32_t size) = 0;

        virtual std::shared_ptr<VertexBuffer> CreateVertexBufferEmpty(
            uint32_t size) = 0;

        virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
            const IndexBufferSpecification& specification,
            const void* indices) = 0;

        virtual std::shared_ptr<IndexBuffer> CreateIndexBufferU32(
            const uint32_t* indices,
            uint32_t count) = 0;

        virtual std::shared_ptr<IndexBuffer> CreateIndexBufferU16(
            const uint16_t* indices,
            uint32_t count) = 0;

        virtual std::shared_ptr<VertexArray> CreateVertexArray() = 0;

        // ---- Async resource creation ----------------------------------------

        virtual std::future<std::shared_ptr<Texture2D>> CreateTexture2DFromFileAsync(
            const std::string& path,
            const TextureSpecification& specification) = 0;

        virtual std::future<std::shared_ptr<Texture2D>> CreateTexture2DFromRGBA8Async(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification) = 0;

        virtual std::future<std::shared_ptr<Framebuffer>> CreateFramebufferAsync(
            const FramebufferSpecification& specification) = 0;

        // ---- Resource thread support -----------------------------------------

        /// Returns true if this backend supports a shared resource context
        /// for parallel GPU resource work (e.g. OpenGL shared contexts).
        virtual bool SupportsSharedResourceContext() const = 0;

        /// Create a secondary context for GPU resource work on a dedicated thread.
        /// Returns nullptr if the backend doesn't support shared resource contexts.
        virtual std::unique_ptr<SharedResourceContext> CreateSharedResourceContext() = 0;

        // ---- Synchronization ------------------------------------------------

        /// Ensure all previously submitted resource work is visible.
        /// On OpenGL this is glFenceSync + glClientWaitSync.
        /// On other APIs this may be a no-op or a device-level wait.
        virtual void SynchronizeResourceVisibility() = 0;

        /// Block until all GPU work completes. Use sparingly.
        virtual void WaitIdle() = 0;

        // ---- Resource retirement ---------------------------------------------

        /// Schedule deferred deletion of a native GPU resource.
        /// Tries the renderer's frame-deferred retirement queue first; falls
        /// back to immediate deletion under a context scope during shutdown.
        ///
        /// @param debugName          Identifier for diagnostics
        /// @param needsPrimaryContext True for resources that must be destroyed on the
        ///                           primary context (e.g. OpenGL VAOs/FBOs).
        /// @param deleteFunc         Backend-specific deletion (called with active context)
        virtual void RetireNativeResource(const char* debugName,
                                          bool needsPrimaryContext,
                                          std::function<void()> deleteFunc) = 0;

        // ---- Thread affinity helpers ----------------------------------------

        /// Returns true if the calling thread is the render thread.
        virtual bool IsOnRenderThread() const = 0;

        /// Returns true if the calling thread currently owns the device context
        /// (e.g. OpenGL context is current on this thread).
        virtual bool IsCurrentOnThisThread() const = 0;

        // ---- Factory --------------------------------------------------------

        static std::unique_ptr<GraphicsDevice> Create(GraphicsAPI api);
    };
}
