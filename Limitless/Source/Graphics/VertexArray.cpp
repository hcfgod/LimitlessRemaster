#include "VertexArray.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"

namespace Limitless
{
    std::shared_ptr<VertexArray> VertexArray::Create()
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL: return std::make_shared<OpenGLVertexArray>();
            default:                  return std::make_shared<OpenGLVertexArray>();
        }
    }
}

