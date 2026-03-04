#pragma once

#include "Graphics/Framebuffer.h"
#include "Graphics/Texture.h"

#include <vector>

#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace Limitless
{
    class OpenGLFramebuffer final : public Framebuffer
    {
    public:
        explicit OpenGLFramebuffer(const FramebufferSpecification& specification);
        ~OpenGLFramebuffer() override;

        void Bind() const override;
        void Unbind() const override;
        std::shared_ptr<Texture2D> GetColorAttachment() const override { return GetColorAttachment(0); }
        std::shared_ptr<Texture2D> GetColorAttachment(uint32_t index) const override;
        uint32_t GetColorAttachmentCount() const override { return static_cast<uint32_t>(m_ColorAttachments.size()); }
        void Resize(uint32_t width, uint32_t height) override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return static_cast<uint32_t>(m_RendererID); }

    private:
        void Invalidate();
        void CreateAttachments();

        FramebufferSpecification m_Specification;
        GLuint m_RendererID = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;

        std::vector<std::shared_ptr<Texture2D>> m_ColorAttachments;
        GLuint m_DepthAttachment = 0;
    };

} // namespace Limitless
