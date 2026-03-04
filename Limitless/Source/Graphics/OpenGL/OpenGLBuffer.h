#pragma once

#include "Graphics/Buffer.h"

#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace Limitless
{
    class OpenGLVertexBuffer final : public VertexBuffer
    {
    public:
        OpenGLVertexBuffer(const void* data, uint32_t size);
        explicit OpenGLVertexBuffer(uint32_t size);
        ~OpenGLVertexBuffer() override;

        void Bind() const override;
        void Unbind() const override;
        void SetData(const void* data, uint32_t size) override;

        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

        GLuint GetRendererID() const { return m_RendererID; }

    private:
        GLuint m_RendererID = 0;
        uint32_t m_SizeBytes = 0;
        BufferLayout m_Layout;
    };

    class OpenGLIndexBuffer final : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(const uint32_t* indices, uint32_t count);
        OpenGLIndexBuffer(const uint16_t* indices, uint32_t count);
        ~OpenGLIndexBuffer() override;

        void Bind() const override;
        void Unbind() const override;
        uint32_t GetCount() const override { return m_Count; }

        GLuint GetRendererID() const { return m_RendererID; }

    private:
        GLuint m_RendererID = 0;
        uint32_t m_Count = 0;
    };
}

