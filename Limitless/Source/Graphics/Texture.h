#pragma once

#include <cstdint>
#include <future>
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

