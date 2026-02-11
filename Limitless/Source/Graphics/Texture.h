#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <span>
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

    struct TextureMipLevelRGBA8View
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        const void* PixelsRGBA8 = nullptr;
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

        // Update sampler-like parameters (filtering/wrapping/mips) for this texture.
        // This is intended to be called from the render thread (via render commands).
        virtual void ApplySpecification(const TextureSpecification& specification) = 0;
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

        // Creates a texture from a full RGBA8 mip chain.
        // - mipLevels[0] must be the base level.
        // - If specification.GenerateMipmaps == true, the mip chain should include all levels needed by the sampler.
        // - This path does NOT call glGenerateMipmap; it uploads each provided level.
        static std::shared_ptr<Texture2D> CreateFromRGBA8MipChain(
            std::span<const TextureMipLevelRGBA8View> mipLevels,
            const TextureSpecification& specification = {});

        // Async variants (non-blocking): schedule GPU work on the render thread resource queue.
        // The returned future completes when the texture has been created/uploaded on the GPU.
        static std::future<std::shared_ptr<Texture2D>> CreateFromFileAsync(
            const std::string& path,
            const TextureSpecification& specification = {});

        static std::future<std::shared_ptr<Texture2D>> CreateFromRGBA8Async(
            uint32_t width,
            uint32_t height,
            const void* rgbaPixels,
            const TextureSpecification& specification = {});
    };
}

