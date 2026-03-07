#include "OpenGLBuffer.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <cstring>

namespace Limitless
{
    namespace
    {
        GLenum ToOpenGLBufferUsage(ResourceUsage usage)
        {
            switch (usage)
            {
                case ResourceUsage::Immutable:
                case ResourceUsage::Default:
                    return GL_STATIC_DRAW;
                case ResourceUsage::Dynamic:
                    return GL_DYNAMIC_DRAW;
                case ResourceUsage::Streaming:
                case ResourceUsage::Transient:
                    return GL_STREAM_DRAW;
                case ResourceUsage::Staging:
                    return GL_DYNAMIC_READ;
                default:
                    return GL_DYNAMIC_DRAW;
            }
        }
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(const BufferSpecification& specification, const void* data)
        : m_SizeBytes(specification.Size)
        , m_Specification(specification)
    {
        LT_VERIFY(m_Specification.Size > 0, "OpenGLVertexBuffer: specification size must be non-zero");
        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_Specification.Size), data, ToOpenGLBufferUsage(m_Specification.Usage));
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(const void* data, uint32_t size)
        : OpenGLVertexBuffer(BufferSpecification{ size, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, data)
    {
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
        : OpenGLVertexBuffer(BufferSpecification{ size, ResourceUsage::Dynamic, MemoryUsage::CpuToGpu }, nullptr)
    {
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
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, ToOpenGLBufferUsage(m_Specification.Usage));
            m_SizeBytes = size;
            m_Specification.Size = size;
        }

        void* dst = glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size),
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        if (dst)
        {
            std::memcpy(dst, data, size);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        else
        {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), data);
        }
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const IndexBufferSpecification& specification, const void* indices)
        : m_Count(specification.Count)
        , m_Specification(specification)
    {
        LT_VERIFY(m_Specification.Count > 0, "OpenGLIndexBuffer: specification count must be non-zero");

        const uint32_t indexSize = [&]() -> uint32_t {
            switch (m_Specification.Type)
            {
                case IndexType::UnsignedByte: return static_cast<uint32_t>(sizeof(uint8_t));
                case IndexType::UnsignedShort: return static_cast<uint32_t>(sizeof(uint16_t));
                case IndexType::UnsignedInt: return static_cast<uint32_t>(sizeof(uint32_t));
                default: return static_cast<uint32_t>(sizeof(uint32_t));
            }
        }();

        glGenBuffers(1, &m_RendererID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(m_Specification.Count * indexSize),
            indices,
            ToOpenGLBufferUsage(m_Specification.Usage));
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const uint32_t* indices, uint32_t count)
        : OpenGLIndexBuffer(IndexBufferSpecification{ count, IndexType::UnsignedInt, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices)
    {
    }

    OpenGLIndexBuffer::OpenGLIndexBuffer(const uint16_t* indices, uint32_t count)
        : OpenGLIndexBuffer(IndexBufferSpecification{ count, IndexType::UnsignedShort, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices)
    {
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

