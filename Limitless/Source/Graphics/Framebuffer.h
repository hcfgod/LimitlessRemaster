#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <vector>

namespace Limitless
{
    class Texture2D;

    /**
     * Framebuffer specification for render-to-texture and off-screen rendering.
     * Used to create framebuffers for viewports, post-processing, and editor UIs.
     */
    struct FramebufferSpecification
    {
        uint32_t Width = 0;
        uint32_t Height = 0;

        /// Number of samples for MSAA. 0 or 1 = no MSAA.
        uint32_t Samples = 1;

        /// Number of color attachments to create.
        uint32_t ColorAttachmentCount = 1;

        /// If true, creates a depth attachment (24-bit depth renderbuffer).
        bool DepthAttachment = true;

        /// If true, creates a stencil attachment (combined with depth when DepthAttachment is true).
        bool StencilAttachment = false;

        /// If true, framebuffer is used as swap chain target (e.g. main window). Default false for off-screen.
        bool SwapChainTarget = false;
    };

    /**
     * Abstract framebuffer interface for render-to-texture and off-screen rendering.
     * Framebuffers must be created on the render thread via Framebuffer::Create().
     */
    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        /// Bind this framebuffer for rendering.
        virtual void Bind() const = 0;

        /// Unbind (bind default framebuffer / backbuffer).
        virtual void Unbind() const = 0;

        /// Get the color attachment texture (slot 0). For use with ImGui::Image, etc.
        virtual std::shared_ptr<Texture2D> GetColorAttachment() const = 0;

        /// Get a color attachment texture by index.
        virtual std::shared_ptr<Texture2D> GetColorAttachment(uint32_t index) const = 0;

        /// Returns number of color attachments.
        virtual uint32_t GetColorAttachmentCount() const = 0;

        /// Resize the framebuffer. Invalidates attachments; they are recreated.
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        /// Renderer handle (OpenGL FBO ID, etc.) for debugging.
        virtual uint32_t GetRendererID() const = 0;

        /// Factory: creates a framebuffer on the render thread. Blocks until complete.
        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& specification);

        /// Async factory: schedules creation on the render thread. Returns a future.
        static std::future<std::shared_ptr<Framebuffer>> CreateAsync(const FramebufferSpecification& specification);
    };

} // namespace Limitless
