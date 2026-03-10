#include "Graphics/RenderPass.h"

#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"

namespace Limitless
{
    void RenderPass::Begin(Renderer& renderer, const RenderPassDescriptor& descriptor)
    {
        renderer.SubmitCommand(std::make_unique<BeginRenderPassCommand>(descriptor));
    }

    void RenderPass::End(Renderer& renderer, const RenderPassDescriptor& descriptor)
    {
        renderer.SubmitCommand(std::make_unique<EndRenderPassCommand>(descriptor));
    }
}
