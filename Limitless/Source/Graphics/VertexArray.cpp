#include "VertexArray.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<VertexArray> VertexArray::Create()
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<VertexArray> {
                    return std::make_shared<OpenGLVertexArray>();
                });
            }
        }
    }
}

