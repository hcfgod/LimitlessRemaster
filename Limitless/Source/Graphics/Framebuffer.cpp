#include "Framebuffer.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Renderer.h"
#include "Core/Error.h"

// Legacy fallback
#include "Graphics/OpenGL/OpenGLFramebuffer.h"

namespace Limitless
{
    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& specification)
    {
        LT_VERIFY(specification.Width > 0 && specification.Height > 0, "Framebuffer dimensions must be non-zero");
        LT_VERIFY(specification.ColorAttachmentCount > 0, "Framebuffer must have at least one color attachment");

        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateFramebuffer(specification);
        }

        // Legacy fallback
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                // FBO-related state must be created on the primary OpenGL context (render thread),
                // not the shared resource thread. See Renderer.h documentation.
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

        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateFramebufferAsync(specification);
        }

        // Legacy fallback
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                // FBO-related state must be created on the primary OpenGL context (render thread),
                // not the shared resource thread. See Renderer.h documentation.
                return renderer.SubmitPrimaryResourceAsync("CreateFramebufferAsync", [specification](GraphicsContext*) -> std::shared_ptr<Framebuffer> {
                    return std::make_shared<OpenGLFramebuffer>(specification);
                });
            }
        }
    }
} // namespace Limitless
