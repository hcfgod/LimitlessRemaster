#include "Graphics/RenderPipeline.h"

#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Renderer.h"

// Legacy fallback
#include "Graphics/OpenGL/OpenGLRenderPipeline.h"

namespace Limitless
{
    std::shared_ptr<RenderPipeline> RenderPipeline::Create(const RenderPipelineDescriptor& descriptor)
    {
        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateRenderPipeline(descriptor);
        }

        // Legacy fallback
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
                return std::make_shared<OpenGLRenderPipeline>(descriptor);
        }
    }
}
