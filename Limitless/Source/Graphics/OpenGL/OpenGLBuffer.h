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
        OpenGLVertexBuffer(const BufferSpecification& specification, const void* data);
        OpenGLVertexBuffer(const void* data, uint32_t size);
        explicit OpenGLVertexBuffer(uint32_t size);
        ~OpenGLVertexBuffer() override;

        void Bind() const override;
        void Unbind() const override;
        void SetData(const void* data, uint32_t size) override;

        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
        const BufferSpecification& GetSpecification() const override { return m_Specification; }
        uintptr_t GetNativeHandle() const override { return static_cast<uintptr_t>(m_RendererID); }

    private:
        GLuint m_RendererID = 0;
        uint32_t m_SizeBytes = 0;
        BufferSpecification m_Specification{};
        BufferLayout m_Layout;
    };

    class OpenGLIndexBuffer final : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(const IndexBufferSpecification& specification, const void* indices);
        OpenGLIndexBuffer(const uint32_t* indices, uint32_t count);
        OpenGLIndexBuffer(const uint16_t* indices, uint32_t count);
        ~OpenGLIndexBuffer() override;

        void Bind() const override;
        void Unbind() const override;
        uint32_t GetCount() const override { return m_Count; }
        const IndexBufferSpecification& GetSpecification() const override { return m_Specification; }
        uintptr_t GetNativeHandle() const override { return static_cast<uintptr_t>(m_RendererID); }

    private:
        GLuint m_RendererID = 0;
        uint32_t m_Count = 0;
        IndexBufferSpecification m_Specification{};
    };
}

