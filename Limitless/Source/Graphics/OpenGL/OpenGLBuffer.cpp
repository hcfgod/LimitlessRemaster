#include "OpenGLBuffer.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    OpenGLVertexBuffer::OpenGLVertexBuffer(const void* data, uint32_t size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint bufferToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait([bufferToDelete](GraphicsContext*) {
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                }
                glDeleteBuffers(1, &m_RendererID);
            }
            m_RendererID = 0;
        }
    }

    void OpenGLVertexBuffer::Bind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLVertexBuffer::Unbind() const
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
        : m_Count(count)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(uint32_t)), indices, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer::~OpenGLIndexBuffer()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint bufferToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait([bufferToDelete](GraphicsContext*) {
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                }
                glDeleteBuffers(1, &m_RendererID);
            }
            m_RendererID = 0;
        }
    }

    void OpenGLIndexBuffer::Bind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLIndexBuffer::Unbind() const
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

