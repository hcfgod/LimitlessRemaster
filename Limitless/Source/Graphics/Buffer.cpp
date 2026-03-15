#include "Buffer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/GraphicsDevice.h"
#include "Core/Error.h"

// Legacy fallback
#include "Graphics/OpenGL/OpenGLBuffer.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<VertexBuffer> VertexBuffer::Create(const BufferSpecification& specification, const void* initialData)
    {
        LT_VERIFY(specification.Size > 0, "VertexBuffer::Create: specification size must be non-zero");

        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateVertexBuffer(specification, initialData);
        }

        // Legacy fallback
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateVertexBuffer", [&](GraphicsContext*) -> std::shared_ptr<VertexBuffer> {
                    return std::make_shared<OpenGLVertexBuffer>(specification, initialData);
                });
            }
        }
    }

    std::shared_ptr<VertexBuffer> VertexBuffer::Create(const void* data, uint32_t size)
    {
        return Create(BufferSpecification{ size, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, data);
    }

    std::shared_ptr<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        return Create(BufferSpecification{ size, ResourceUsage::Dynamic, MemoryUsage::CpuToGpu }, nullptr);
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(const IndexBufferSpecification& specification, const void* indices)
    {
        LT_VERIFY(specification.Count > 0, "IndexBuffer::Create: specification count must be non-zero");

        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateIndexBuffer(specification, indices);
        }

        // Legacy fallback
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateIndexBuffer", [&](GraphicsContext*) -> std::shared_ptr<IndexBuffer> {
                    return std::make_shared<OpenGLIndexBuffer>(specification, indices);
                });
            }
        }
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(const uint32_t* indices, uint32_t count)
    {
        return Create(IndexBufferSpecification{ count, IndexType::UnsignedInt, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices);
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(const uint16_t* indices, uint32_t count)
    {
        return Create(IndexBufferSpecification{ count, IndexType::UnsignedShort, ResourceUsage::Immutable, MemoryUsage::GpuOnly }, indices);
    }
}

