#include "Buffer.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Core/Error.h"

#include "Graphics/OpenGL/OpenGLBuffer.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<VertexBuffer> VertexBuffer::Create(const void* data, uint32_t size)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<VertexBuffer> {
                    return std::make_shared<OpenGLVertexBuffer>(data, size);
                });
            }
        }
    }

    std::shared_ptr<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<VertexBuffer> {
                    return std::make_shared<OpenGLVertexBuffer>(size);
                });
            }
        }
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(const uint32_t* indices, uint32_t count)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<IndexBuffer> {
                    return std::make_shared<OpenGLIndexBuffer>(indices, count);
                });
            }
        }
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(const uint16_t* indices, uint32_t count)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<IndexBuffer> {
                    return std::make_shared<OpenGLIndexBuffer>(indices, count);
                });
            }
        }
    }
}

