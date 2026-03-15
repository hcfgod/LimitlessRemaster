#pragma once

#include "Graphics/GraphicsDevice.h"

#include <atomic>
#include <memory>
#include <thread>

namespace Limitless
{
    class OpenGLContext;
    class OpenGLSharedContext;
    class RenderResourceThread;

    class OpenGLDevice final : public GraphicsDevice
    {
    public:
        OpenGLDevice();
        ~OpenGLDevice() override;

        // ---- Identity -------------------------------------------------------
        GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }

        // ---- Capabilities ---------------------------------------------------
        GfxDeviceCapabilities GetCapabilities() const override;

        // ---- Context / lifecycle --------------------------------------------
        GraphicsContext* GetContext() const override;
        void Initialize(void* nativeWindow) override;
        void Shutdown() override;

        // ---- Resource creation (synchronous) --------------------------------
        std::shared_ptr<Shader> CreateShaderFromSource(
            const std::string& name,
            const std::string& vertexSource,
            const std::string& fragmentSource) override;

        std::shared_ptr<Shader> CreateShaderFromDescriptor(
            const ShaderDescriptor& descriptor) override;

        std::shared_ptr<Texture2D> CreateTexture2DFromFile(
            const std::string& path,
            const TextureSpecification& specification) override;

        std::shared_ptr<Texture2D> CreateTexture2DFromRGBA8(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification) override;

        std::shared_ptr<Texture2D> CreateTexture2DFromMipChain(
            const TextureMipLevelRGBA8View* mipLevels,
            uint32_t mipLevelCount,
            const TextureSpecification& specification) override;

        std::shared_ptr<Texture2D> CreateTexture2DForRenderTarget(
            uint32_t width,
            uint32_t height) override;

        std::shared_ptr<Framebuffer> CreateFramebuffer(
            const FramebufferSpecification& specification) override;

        std::shared_ptr<RenderPipeline> CreateRenderPipeline(
            const RenderPipelineDescriptor& descriptor) override;

        std::shared_ptr<VertexBuffer> CreateVertexBuffer(
            const BufferSpecification& specification,
            const void* initialData) override;

        std::shared_ptr<VertexBuffer> CreateVertexBufferRaw(
            const void* data,
            uint32_t size) override;

        std::shared_ptr<VertexBuffer> CreateVertexBufferEmpty(
            uint32_t size) override;

        std::shared_ptr<IndexBuffer> CreateIndexBuffer(
            const IndexBufferSpecification& specification,
            const void* indices) override;

        std::shared_ptr<IndexBuffer> CreateIndexBufferU32(
            const uint32_t* indices,
            uint32_t count) override;

        std::shared_ptr<IndexBuffer> CreateIndexBufferU16(
            const uint16_t* indices,
            uint32_t count) override;

        std::shared_ptr<VertexArray> CreateVertexArray() override;

        // ---- Async resource creation ----------------------------------------
        std::future<std::shared_ptr<Texture2D>> CreateTexture2DFromFileAsync(
            const std::string& path,
            const TextureSpecification& specification) override;

        std::future<std::shared_ptr<Texture2D>> CreateTexture2DFromRGBA8Async(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification) override;

        std::future<std::shared_ptr<Framebuffer>> CreateFramebufferAsync(
            const FramebufferSpecification& specification) override;

        // ---- Resource thread support ----------------------------------------
        bool SupportsSharedResourceContext() const override;
        std::unique_ptr<SharedResourceContext> CreateSharedResourceContext() override;

        // ---- Synchronization ------------------------------------------------
        void SynchronizeResourceVisibility() override;
        void WaitIdle() override;

        // ---- Resource retirement --------------------------------------------
        void RetireNativeResource(const char* debugName,
                                  bool needsPrimaryContext,
                                  std::function<void()> deleteFunc) override;

        // ---- Thread affinity ------------------------------------------------
        bool IsOnRenderThread() const override;
        bool IsCurrentOnThisThread() const override;

        // ---- OpenGL-specific accessors (for Renderer migration) -------------
        OpenGLContext* GetOpenGLContext() const { return m_Context; }

    private:
        OpenGLContext* m_Context = nullptr; // borrowed, not owned
        bool m_Initialized = false;
    };
}
