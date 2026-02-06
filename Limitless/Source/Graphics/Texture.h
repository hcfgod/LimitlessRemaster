#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Limitless
{
    enum class TextureFilter
    {
        Nearest = 0,
        Linear = 1
    };

    enum class TextureWrap
    {
        Repeat = 0,
        ClampToEdge = 1
    };

    struct TextureSpecification
    {
        TextureFilter MinFilter = TextureFilter::Linear;
        TextureFilter MagFilter = TextureFilter::Linear;
        TextureWrap WrapU = TextureWrap::Repeat;
        TextureWrap WrapV = TextureWrap::Repeat;
        bool GenerateMipmaps = true;
        bool FlipVerticallyOnLoad = true;
    };

    class Texture
    {
    public:
        virtual ~Texture() = default;

        virtual void Bind(uint32_t slot) const = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        // Renderer handle (OpenGL texture ID, etc.) for debugging and low-level bridging.
        virtual uint32_t GetRendererID() const = 0;
    };

    class Texture2D : public Texture
    {
    public:
        static std::shared_ptr<Texture2D> CreateFromFile(
            const std::string& path,
            const TextureSpecification& specification = {});

        static std::shared_ptr<Texture2D> CreateFromRGBA8(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification = {});
    };
}

