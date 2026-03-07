#include "Graphics/RenderPipeline.h"

#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLRenderPipeline.h"

namespace Limitless
{
    std::shared_ptr<RenderPipeline> RenderPipeline::Create(const RenderPipelineDescriptor& descriptor)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
                return std::make_shared<OpenGLRenderPipeline>(descriptor);
        }
    }
}
