#include "OpenGLVertexArray.h"
#include "OpenGLBuffer.h"
#include "Core/Error.h"

namespace Limitless
{
    static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float:
            case ShaderDataType::Float2:
            case ShaderDataType::Float3:
            case ShaderDataType::Float4:
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
                return GL_FLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
                return GL_INT;
            case ShaderDataType::Bool:
                return GL_BOOL;
            default:
                return GL_FLOAT;
        }
    }

    OpenGLVertexArray::OpenGLVertexArray()
    {
        glGenVertexArrays(1, &m_RendererID);
    }

    OpenGLVertexArray::~OpenGLVertexArray()
    {
        if (m_RendererID)
        {
            glDeleteVertexArrays(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void OpenGLVertexArray::Bind() const
    {
        glBindVertexArray(m_RendererID);
    }

    void OpenGLVertexArray::Unbind() const
    {
        glBindVertexArray(0);
    }

    void OpenGLVertexArray::AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer)
    {
        LT_VERIFY(vertexBuffer != nullptr, "VertexBuffer cannot be null");
        const auto& layout = vertexBuffer->GetLayout();
        LT_VERIFY(layout.GetElements().size() > 0, "VertexBuffer has no layout");

        Bind();
        vertexBuffer->Bind();

        for (const auto& element : layout)
        {
            glEnableVertexAttribArray(m_VertexAttribIndex);
            glVertexAttribPointer(
                m_VertexAttribIndex,
                static_cast<GLint>(element.GetComponentCount()),
                ShaderDataTypeToOpenGLBaseType(element.Type),
                element.Normalized ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(layout.GetStride()),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
            m_VertexAttribIndex++;
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
    {
        LT_VERIFY(indexBuffer != nullptr, "IndexBuffer cannot be null");

        Bind();
        indexBuffer->Bind();
        m_IndexBuffer = indexBuffer;
    }
}

