#include "Texture.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Core/Error.h"

#include "Graphics/OpenGL/OpenGLTexture.h"

namespace Limitless
{
    std::shared_ptr<Texture2D> Texture2D::CreateFromFile(const std::string& path, const TextureSpecification& specification)
    {
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL: return std::make_shared<OpenGLTexture2D>(path, specification);
            default:                  return std::make_shared<OpenGLTexture2D>(path, specification);
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
            case GraphicsAPI::OpenGL: return std::make_shared<OpenGLTexture2D>(width, height, rgbaPixels, specification);
            default:                  return std::make_shared<OpenGLTexture2D>(width, height, rgbaPixels, specification);
        }
    }
}

