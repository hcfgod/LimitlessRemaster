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
        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateTexture2D/FromFile", [&](GraphicsContext*) -> std::shared_ptr<Texture2D> {
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
        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                LT_VERIFY(rgbaPixels != nullptr, "Texture2D::CreateFromRGBA8: rgbaPixels is null");
                LT_VERIFY(width > 0 && height > 0, "Texture2D::CreateFromRGBA8: texture size must be non-zero");

                // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
                std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                                static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));
                return renderer.SubmitResourceAndWait("CreateTexture2D/FromRGBA8", [=](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
                    return std::make_shared<OpenGLTexture2D>(width, height, pixelBytes.data(), specification);
                });
            }
        }
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromRGBA8MipChain(
        const std::span<const TextureMipLevelRGBA8View> mipLevels,
        const TextureSpecification& specification)
    {
        if (mipLevels.empty())
        {
            LT_VERIFY(false, "Texture2D::CreateFromRGBA8MipChain: mipLevels is empty");
            return nullptr;
        }

        for (const auto& mip : mipLevels)
        {
            LT_VERIFY(mip.Width > 0 && mip.Height > 0, "Texture2D::CreateFromRGBA8MipChain: invalid mip dimensions");
            LT_VERIFY(mip.PixelsRGBA8 != nullptr, "Texture2D::CreateFromRGBA8MipChain: mip pixels are null");
        }

        // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
        struct CopiedMip
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            size_t Offset = 0;
        };

        std::vector<CopiedMip> copied;
        copied.reserve(mipLevels.size());

        std::vector<uint8_t> allPixels;
        allPixels.reserve(static_cast<size_t>(mipLevels[0].Width) * static_cast<size_t>(mipLevels[0].Height) * 4u);

        for (const auto& mip : mipLevels)
        {
            const size_t sizeBytes = static_cast<size_t>(mip.Width) * static_cast<size_t>(mip.Height) * 4u;
            const size_t offset = allPixels.size();
            const uint8_t* src = static_cast<const uint8_t*>(mip.PixelsRGBA8);

            allPixels.insert(allPixels.end(), src, src + sizeBytes);
            copied.push_back(CopiedMip{ mip.Width, mip.Height, offset });
        }

        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateTexture2D/FromRGBA8MipChain", [specification, copied = std::move(copied), pixels = std::move(allPixels)](GraphicsContext*) mutable -> std::shared_ptr<Texture2D> {
                    std::vector<TextureMipLevelRGBA8View> views;
                    views.reserve(copied.size());
                    for (const auto& m : copied)
                    {
                        TextureMipLevelRGBA8View v;
                        v.Width = m.Width;
                        v.Height = m.Height;
                        v.PixelsRGBA8 = pixels.data() + m.Offset;
                        views.push_back(v);
                    }
                    return std::make_shared<OpenGLTexture2D>(std::span<const TextureMipLevelRGBA8View>(views.data(), views.size()), specification);
                });
            }
        }
    }

    std::future<std::shared_ptr<Texture2D>> Texture2D::CreateFromFileAsync(
        const std::string& path,
        const TextureSpecification& specification)
    {
        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAsync("CreateTexture2DAsync/FromFile", [path, specification](GraphicsContext*) -> std::shared_ptr<Texture2D> {
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
        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                LT_VERIFY(rgbaPixels != nullptr, "Texture2D::CreateFromRGBA8Async: rgbaPixels is null");
                LT_VERIFY(width > 0 && height > 0, "Texture2D::CreateFromRGBA8Async: texture size must be non-zero");

                // Copy pixels for safety: caller memory may go out of scope before the render thread executes.
                std::vector<uint8_t> pixelBytes(static_cast<const uint8_t*>(rgbaPixels),
                                                static_cast<const uint8_t*>(rgbaPixels) + (width * height * 4));

                return renderer.SubmitResourceAsync("CreateTexture2DAsync/FromRGBA8", [width, height, specification, pixels = std::move(pixelBytes)](GraphicsContext*) mutable
                    -> std::shared_ptr<Texture2D>
                {
                    return std::make_shared<OpenGLTexture2D>(width, height, pixels.data(), specification);
                });
            }
        }
    }

    std::shared_ptr<Texture2D> Texture2D::CreateForRenderTarget(uint32_t width, uint32_t height)
    {
        LT_VERIFY(width > 0 && height > 0, "Texture2D::CreateForRenderTarget: dimensions must be non-zero");

        auto& renderer = Renderer::GetInstance();
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateTexture2D/ForRenderTarget", [width, height](GraphicsContext*) -> std::shared_ptr<Texture2D> {
                    return std::make_shared<OpenGLTexture2D>(width, height);
                });
            }
        }
    }
}

