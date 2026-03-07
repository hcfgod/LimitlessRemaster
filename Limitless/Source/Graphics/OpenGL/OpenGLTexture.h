#pragma once

#include "Graphics/Texture.h"

#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace Limitless
{
    class OpenGLTexture2D final : public Texture2D
    {
    public:
        OpenGLTexture2D(const std::string& path, TextureSpecification specification);
        OpenGLTexture2D(uint32_t width, uint32_t height, const void* rgbaPixels, TextureSpecification specification);
        OpenGLTexture2D(std::span<const TextureMipLevelRGBA8View> mipLevels, TextureSpecification specification);
        /// Creates an empty texture for framebuffer color attachment (render target).
        OpenGLTexture2D(uint32_t width, uint32_t height);
        ~OpenGLTexture2D() override;

        void Bind(uint32_t slot) const override;
        void ApplySpecification(const TextureSpecification& specification) override;

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        const TextureSpecification& GetSpecification() const override { return m_Specification; }
        uintptr_t GetNativeHandle() const override { return static_cast<uintptr_t>(m_RendererID); }

    private:
        void ApplyParameters() const;

        GLuint m_RendererID = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        TextureSpecification m_Specification{};
    };
}

