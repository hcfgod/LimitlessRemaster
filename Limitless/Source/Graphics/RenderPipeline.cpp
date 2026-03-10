#include "Graphics/RenderPipeline.h"

#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLRenderPipeline.h"
#include "Graphics/Renderer.h"

namespace Limitless
{
    std::shared_ptr<RenderPipeline> RenderPipeline::Create(const RenderPipelineDescriptor& descriptor)
    {
        const GraphicsAPI api = Renderer::GetInstance().GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
                return std::make_shared<OpenGLRenderPipeline>(descriptor);
        }
    }
}
