#include "OpenGLBuffer.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <cstring>

namespace Limitless
{
    OpenGLVertexBuffer::OpenGLVertexBuffer(const void* data, uint32_t size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
        m_SizeBytes = size;
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
        m_SizeBytes = size;
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint bufferToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait("OpenGLBuffer/DeleteBuffer", [bufferToDelete](GraphicsContext*) {
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                }
                else
                {
                    LT_CORE_WARN("OpenGLVertexBuffer destroyed after renderer/context teardown; leaking GL buffer {}", bufferToDelete);
                }
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
        if (size > m_SizeBytes)
        {
            // Grow the buffer if needed (rare for our streaming use cases).
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
            m_SizeBytes = size;
        }

        // Prefer a map+memcpy path for dynamic streaming buffers:
        // - GL_MAP_INVALIDATE_BUFFER_BIT avoids sync hazards by allowing the driver to "orphan"
        //   the old storage.
        // - This tends to reduce driver overhead vs. repeated glBufferSubData for large uploads.
        void* dst = glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size),
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        if (dst)
        {
            std::memcpy(dst, data, size);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        else
        {
            // Fallback: should be rare, but keep it safe.
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
        }
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
        : m_Count(count)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(uint32_t)), indices, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const uint16_t* indices, uint32_t count)
        : m_Count(count)
    {
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(uint16_t)), indices, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer::~OpenGLIndexBuffer()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint bufferToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait("OpenGLBuffer/DeleteBuffer", [bufferToDelete](GraphicsContext*) {
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    GLuint id = bufferToDelete;
                    glDeleteBuffers(1, &id);
                }
                else
                {
                    LT_CORE_WARN("OpenGLIndexBuffer destroyed after renderer/context teardown; leaking GL buffer {}", bufferToDelete);
                }
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

