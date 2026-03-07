#include "OpenGLFramebuffer.h"
#include "Graphics/Texture.h"
#include "Graphics/OpenGL/OpenGLTexture.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

#include <cassert>
#include <algorithm>
#include <array>

namespace Limitless
{
    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& specification)
        : m_Specification(specification)
        , m_Width(specification.Width)
        , m_Height(specification.Height)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        Invalidate();
    }

    OpenGLFramebuffer::~OpenGLFramebuffer()
    {
        if (m_RendererID != 0)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint fboToDelete = m_RendererID;
            const GLuint depthToDelete = m_DepthAttachment;

            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                // FBO deletion must happen on primary context (same as creation).
                renderer.SubmitPrimaryResourceAndWait("OpenGLFramebuffer/Delete", [fboToDelete, depthToDelete](GraphicsContext*) {
                    if (depthToDelete != 0)
                    {
                        glDeleteRenderbuffers(1, &depthToDelete);
                    }
                    GLuint id = fboToDelete;
                    glDeleteFramebuffers(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    if (depthToDelete != 0)
                    {
                        glDeleteRenderbuffers(1, &depthToDelete);
                    }
                    GLuint id = fboToDelete;
                    glDeleteFramebuffers(1, &id);
                }
                else
                {
                    LT_CORE_WARN("OpenGLFramebuffer destroyed after renderer/context teardown; leaking GL FBO {}", fboToDelete);
                }
            }
            m_RendererID = 0;
            m_DepthAttachment = 0;
        }
    }

    void OpenGLFramebuffer::Invalidate()
    {
        if (m_RendererID != 0)
        {
            glDeleteFramebuffers(1, &m_RendererID);
            if (m_DepthAttachment != 0)
            {
                glDeleteRenderbuffers(1, &m_DepthAttachment);
                m_DepthAttachment = 0;
            }
            m_RendererID = 0;
        }

        glGenFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        CreateAttachments();

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LT_CORE_ERROR("OpenGLFramebuffer incomplete: 0x{:x}", status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            LT_THROW_GRAPHICS_ERROR("Framebuffer creation failed");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::CreateAttachments()
    {
        // Direct creation: we are already on the render thread (called from Framebuffer::Create).
        // Texture2D::CreateForRenderTarget would deadlock (SubmitResourceAndWait from render thread).
        m_ColorAttachments.clear();
        m_ColorAttachments.reserve(m_Specification.ColorAttachmentCount);

        std::array<GLenum, 8> drawBuffers{};
        const uint32_t colorAttachmentCount = std::min<uint32_t>(m_Specification.ColorAttachmentCount, static_cast<uint32_t>(drawBuffers.size()));
        for (uint32_t index = 0; index < colorAttachmentCount; ++index)
        {
            auto colorAttachment = std::make_shared<OpenGLTexture2D>(m_Width, m_Height);
            const auto* glTexture = static_cast<const OpenGLTexture2D*>(colorAttachment.get());
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_TEXTURE_2D, static_cast<GLuint>(glTexture->GetNativeHandle()), 0);
            m_ColorAttachments.push_back(std::move(colorAttachment));
            drawBuffers[index] = GL_COLOR_ATTACHMENT0 + index;
        }

        if (!m_ColorAttachments.empty())
            glDrawBuffers(static_cast<GLsizei>(m_ColorAttachments.size()), drawBuffers.data());

        if (m_Specification.DepthAttachment)
        {
            GLenum depthFormat = GL_DEPTH_COMPONENT24;
            if (m_Specification.StencilAttachment)
            {
                depthFormat = GL_DEPTH24_STENCIL8;
            }

            glGenRenderbuffers(1, &m_DepthAttachment);
            glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
            glRenderbufferStorage(GL_RENDERBUFFER, depthFormat, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);

            if (m_Specification.StencilAttachment)
            {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);
            }

            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
    }

    void OpenGLFramebuffer::Bind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));
    }

    void OpenGLFramebuffer::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LT_CORE_WARN("OpenGLFramebuffer::Resize ignored invalid size {}x{}", width, height);
            return;
        }

        auto resizeOnPrimaryContext = [this, width, height](GraphicsContext*) {
            uint32_t targetWidth = width;
            uint32_t targetHeight = height;

            GLint maxRenderbufferSize = 0;
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
            if (maxRenderbufferSize > 0)
            {
                const uint32_t maxSize = static_cast<uint32_t>(maxRenderbufferSize);
                const uint32_t clampedWidth = std::min(targetWidth, maxSize);
                const uint32_t clampedHeight = std::min(targetHeight, maxSize);
                if (clampedWidth != targetWidth || clampedHeight != targetHeight)
                {
                    LT_CORE_WARN("OpenGLFramebuffer::Resize clamped {}x{} to {}x{} (GL_MAX_RENDERBUFFER_SIZE={})",
                                 targetWidth,
                                 targetHeight,
                                 clampedWidth,
                                 clampedHeight,
                                 maxRenderbufferSize);
                    targetWidth = clampedWidth;
                    targetHeight = clampedHeight;
                }
            }

            if (targetWidth == m_Width && targetHeight == m_Height)
                return;

            m_Width = targetWidth;
            m_Height = targetHeight;
            m_Specification.Width = targetWidth;
            m_Specification.Height = targetHeight;
            Invalidate();
        };

        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized())
        {
            renderer.SubmitPrimaryResourceAndWait("OpenGLFramebuffer/Resize", resizeOnPrimaryContext);
            return;
        }

        // Single-thread fallback for very early startup paths before renderer init.
        resizeOnPrimaryContext(nullptr);
    }

    std::shared_ptr<Texture2D> OpenGLFramebuffer::GetColorAttachment(uint32_t index) const
    {
        if (index >= m_ColorAttachments.size())
            return nullptr;
        return m_ColorAttachments[index];
    }
} // namespace Limitless
