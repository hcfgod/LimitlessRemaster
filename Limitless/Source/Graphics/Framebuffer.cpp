#include "Framebuffer.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/Renderer.h"
#include "Core/Error.h"

#include "Graphics/OpenGL/OpenGLFramebuffer.h"

namespace Limitless
{
    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& specification)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        LT_VERIFY(specification.ColorAttachmentCount > 0, "Framebuffer must have at least one color attachment");

        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                // FBO-related state must be created on the primary OpenGL context (render thread),
                // not the shared resource thread. See Renderer.h documentation.
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitPrimaryResourceAndWait("CreateFramebuffer", [specification](GraphicsContext*) -> std::shared_ptr<Framebuffer> {
                    return std::make_shared<OpenGLFramebuffer>(specification);
                });
            }
        }
    }

    std::future<std::shared_ptr<Framebuffer>> Framebuffer::CreateAsync(const FramebufferSpecification& specification)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        LT_VERIFY(specification.ColorAttachmentCount > 0, "Framebuffer must have at least one color attachment");

        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                // FBO-related state must be created on the primary OpenGL context (render thread),
                // not the shared resource thread. See Renderer.h documentation.
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitPrimaryResourceAsync("CreateFramebufferAsync", [specification](GraphicsContext*) -> std::shared_ptr<Framebuffer> {
                    return std::make_shared<OpenGLFramebuffer>(specification);
                });
            }
        }
    }
} // namespace Limitless
