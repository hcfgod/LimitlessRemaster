#include "Graphics/RenderPass.h"

#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"

namespace Limitless
{
    namespace
    {
        ClearCommand::ClearFlags BuildClearFlags(const RenderPassDescriptor& descriptor)
        {
            ClearCommand::ClearFlags flags{};
            flags.color = false;
            flags.depth = false;
            flags.stencil = false;
            for (const auto& colorAttachment : descriptor.ColorAttachments)
            {
                if (colorAttachment.LoadAction == RenderLoadAction::Clear)
                {
                    flags.color = true;
                    break;
                }
            }

            if (descriptor.DepthStencilAttachment.has_value())
            {
                flags.depth = descriptor.DepthStencilAttachment->DepthLoadAction == RenderLoadAction::Clear;
                flags.stencil = descriptor.DepthStencilAttachment->StencilLoadAction == RenderLoadAction::Clear;
            }

            return flags;
        }
    }

    void RenderPass::Begin(Renderer& renderer, const RenderPassDescriptor& descriptor)
    {
        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(descriptor.TargetFramebuffer));
        renderer.SubmitCommand(std::make_unique<SetViewportCommand>(
            descriptor.Viewport.X,
            descriptor.Viewport.Y,
            descriptor.Viewport.Width,
            descriptor.Viewport.Height));

        if (descriptor.ScissorRect.has_value())
        {
            renderer.SubmitCommand(std::make_unique<SetScissorCommand>(
                descriptor.ScissorRect->X,
                descriptor.ScissorRect->Y,
                descriptor.ScissorRect->Width,
                descriptor.ScissorRect->Height,
                true));
        }
        else
        {
            renderer.SubmitCommand(std::make_unique<SetScissorCommand>(0, 0, 0, 0, false));
        }

        const ClearCommand::ClearFlags clearFlags = BuildClearFlags(descriptor);
        if (clearFlags.color || clearFlags.depth || clearFlags.stencil)
        {
            glm::vec4 clearColor = glm::vec4(0.0f);
            for (const auto& colorAttachment : descriptor.ColorAttachments)
            {
                if (colorAttachment.LoadAction == RenderLoadAction::Clear)
                {
                    clearColor = colorAttachment.ClearColor;
                    break;
                }
            }

            renderer.SubmitCommand(std::make_unique<ClearCommand>(
                clearFlags,
                clearColor.r,
                clearColor.g,
                clearColor.b,
                clearColor.a));
        }
    }

    void RenderPass::End(Renderer& renderer, const RenderPassDescriptor& descriptor)
    {
        if (descriptor.ScissorRect.has_value())
            renderer.SubmitCommand(std::make_unique<SetScissorCommand>(0, 0, 0, 0, false));
    }
}
