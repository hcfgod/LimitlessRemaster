#include "Texture.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Core/Error.h"

#include "Graphics/OpenGL/OpenGLTexture.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <vector>

namespace Limitless
{
    std::shared_ptr<Texture2D> Texture2D::CreateFromFile(const std::string& path, const TextureSpecification& specification)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait([&](GraphicsContext*) -> std::shared_ptr<Texture2D> {
                    return std::make_shared<OpenGLTexture2D>(path, specification);
                });
            }
        }
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromRGBA8(
        uint32_t width,
        uint32_t height,
        const void* rgbaPixels,
        const TextureSpecification& specification)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                LT_VERIFY(rgbaPixels != nullptr, "Texture2D::CreateFromRGBA8: rgbaPixels is null");
                LT_VERIFY(width > 0 && height > 0, "Texture2D::CreateFromRGBA8: texture size must be non-zero");

                // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
                std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                                static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));
                return renderer.SubmitResourceAndWait([=](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
                    return std::make_shared<OpenGLTexture2D>(width, height, pixelBytes.data(), specification);
                });
            }
        }
    }

    std::future<std::shared_ptr<Texture2D>> Texture2D::CreateFromFileAsync(
        const std::string& path,
        const TextureSpecification& specification)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAsync([path, specification](GraphicsContext*) -> std::shared_ptr<Texture2D> {
                    return std::make_shared<OpenGLTexture2D>(path, specification);
                });
            }
        }
    }

    std::future<std::shared_ptr<Texture2D>> Texture2D::CreateFromRGBA8Async(
        uint32_t width,
        uint32_t height,
        const void* rgbaPixels,
        const TextureSpecification& specification)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                LT_VERIFY(rgbaPixels != nullptr, "Texture2D::CreateFromRGBA8Async: rgbaPixels is null");
                LT_VERIFY(width > 0 && height > 0, "Texture2D::CreateFromRGBA8Async: texture size must be non-zero");

                auto& renderer = Renderer::GetInstance();
                // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
                std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                                static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));

                return renderer.SubmitResourceAsync([width, height, specification, pixels = std::move(pixelBytes)](GraphicsContext*) mutable
                    -> std::shared_ptr<Texture2D>
                {
                    return std::make_shared<OpenGLTexture2D>(width, height, pixels.data(), specification);
                });
            }
        }
    }
}

