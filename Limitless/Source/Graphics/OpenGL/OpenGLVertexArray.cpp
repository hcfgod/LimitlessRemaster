#include "OpenGLVertexArray.h"
#include "OpenGLBuffer.h"
#include "Core/Error.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    static bool IsIntegerType(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Int:
            case ShaderDataType::Int2:
            case ShaderDataType::Int3:
            case ShaderDataType::Int4:
            case ShaderDataType::Bool:
                return true;
            default:
                return false;
        }
    }

    static bool IsMatrixType(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
                return true;
            default:
                return false;
        }
    }

    static uint32_t MatrixRowCount(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Mat3: return 3;
            case ShaderDataType::Mat4: return 4;
            default:                   return 0;
        }
    }

    static uint32_t MatrixColumnCount(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Mat3: return 3;
            case ShaderDataType::Mat4: return 4;
            default:                   return 0;
        }
    }

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
                // OpenGL vertex attributes do not have a true boolean attribute format; treat as uint8.
                return GL_UNSIGNED_BYTE;
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
            auto& renderer = Renderer::GetInstance();
            const GLuint vaoToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait([vaoToDelete](GraphicsContext*) {
                    GLuint id = vaoToDelete;
                    glDeleteVertexArrays(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    GLuint id = vaoToDelete;
                    glDeleteVertexArrays(1, &id);
                }
                else
                {
                    LT_CORE_WARN("OpenGLVertexArray destroyed after renderer/context teardown; leaking GL VAO {}", vaoToDelete);
                }
            }
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
        // Route GPU state changes through the renderer's resource command queue when available.
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait([&](GraphicsContext*) {
                LT_VERIFY(vertexBuffer != nullptr, "VertexBuffer cannot be null");
                const auto& layout = vertexBuffer->GetLayout();
                LT_VERIFY(layout.GetElements().size() > 0, "VertexBuffer has no layout");

                Bind();
                vertexBuffer->Bind();

                for (const auto& element : layout)
                {
                    const GLenum baseType = ShaderDataTypeToOpenGLBaseType(element.Type);

                    if (IsMatrixType(element.Type))
                    {
                        // OpenGL matrix attributes are specified as N separate vecN attributes.
                        const uint32_t rows = MatrixRowCount(element.Type);
                        const uint32_t cols = MatrixColumnCount(element.Type);
                        LT_VERIFY(rows > 0 && cols > 0, "Invalid matrix shader data type");

                        for (uint32_t col = 0; col < cols; ++col)
                        {
                            glEnableVertexAttribArray(m_VertexAttribIndex);
                            glVertexAttribPointer(
                                m_VertexAttribIndex,
                                static_cast<GLint>(rows),
                                GL_FLOAT,
                                element.Normalized ? GL_TRUE : GL_FALSE,
                                static_cast<GLsizei>(layout.GetStride()),
                                reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset + sizeof(float) * rows * col)));
                            m_VertexAttribIndex++;
                        }

                        continue;
                    }

                    glEnableVertexAttribArray(m_VertexAttribIndex);

                    if (IsIntegerType(element.Type))
                    {
                        glVertexAttribIPointer(
                            m_VertexAttribIndex,
                            static_cast<GLint>(element.GetComponentCount()),
                            baseType,
                            static_cast<GLsizei>(layout.GetStride()),
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
                    }
                    else
                    {
                        glVertexAttribPointer(
                            m_VertexAttribIndex,
                            static_cast<GLint>(element.GetComponentCount()),
                            baseType,
                            element.Normalized ? GL_TRUE : GL_FALSE,
                            static_cast<GLsizei>(layout.GetStride()),
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
                    }

                    m_VertexAttribIndex++;
                }

                m_VertexBuffers.push_back(vertexBuffer);
            });
            return;
        }

        // Fallback: execute immediately on the calling thread (requires a current context).
        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        LT_VERIFY(vertexBuffer != nullptr, "VertexBuffer cannot be null");
        const auto& layout = vertexBuffer->GetLayout();
        LT_VERIFY(layout.GetElements().size() > 0, "VertexBuffer has no layout");

        Bind();
        vertexBuffer->Bind();

        for (const auto& element : layout)
        {
            const GLenum baseType = ShaderDataTypeToOpenGLBaseType(element.Type);

            if (IsMatrixType(element.Type))
            {
                // OpenGL matrix attributes are specified as N separate vecN attributes.
                const uint32_t rows = MatrixRowCount(element.Type);
                const uint32_t cols = MatrixColumnCount(element.Type);
                LT_VERIFY(rows > 0 && cols > 0, "Invalid matrix shader data type");

                for (uint32_t col = 0; col < cols; ++col)
                {
                    glEnableVertexAttribArray(m_VertexAttribIndex);
                    glVertexAttribPointer(
                        m_VertexAttribIndex,
                        static_cast<GLint>(rows),
                        GL_FLOAT,
                        element.Normalized ? GL_TRUE : GL_FALSE,
                        static_cast<GLsizei>(layout.GetStride()),
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset + sizeof(float) * rows * col)));
                    m_VertexAttribIndex++;
                }

                continue;
            }

            glEnableVertexAttribArray(m_VertexAttribIndex);

            if (IsIntegerType(element.Type))
            {
                glVertexAttribIPointer(
                    m_VertexAttribIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    baseType,
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
            }
            else
            {
                glVertexAttribPointer(
                    m_VertexAttribIndex,
                    static_cast<GLint>(element.GetComponentCount()),
                    baseType,
                    element.Normalized ? GL_TRUE : GL_FALSE,
                    static_cast<GLsizei>(layout.GetStride()),
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(element.Offset)));
            }

            m_VertexAttribIndex++;
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void OpenGLVertexArray::SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait([&](GraphicsContext*) {
                LT_VERIFY(indexBuffer != nullptr, "IndexBuffer cannot be null");

                Bind();
                indexBuffer->Bind();
                m_IndexBuffer = indexBuffer;
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        LT_VERIFY(indexBuffer != nullptr, "IndexBuffer cannot be null");

        Bind();
        indexBuffer->Bind();
        m_IndexBuffer = indexBuffer;
    }
}

