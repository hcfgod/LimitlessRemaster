#include "VertexArray.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLVertexArray.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<VertexArray> VertexArray::Create()
    {
        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                // VAOs are not shared across OpenGL contexts; creation must happen on the primary context.
                return renderer.SubmitPrimaryResourceAndWait("CreateVertexArray", [&](GraphicsContext*) -> std::shared_ptr<VertexArray> {
                    return std::make_shared<OpenGLVertexArray>();
                });
            }
        }
    }
}

