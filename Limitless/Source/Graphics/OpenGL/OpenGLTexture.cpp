#include "OpenGLTexture.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

#include "stb/stb_image/stb_image.h"
#include <string>

namespace Limitless
{
    static GLint ToOpenGLFilter(TextureFilter filter)
    {
        switch (filter)
        {
            case TextureFilter::Nearest: return GL_NEAREST;
            case TextureFilter::Linear:  return GL_LINEAR;
            default:                     return GL_LINEAR;
        }
    }

    static GLint ToOpenGLWrap(TextureWrap wrap)
    {
        switch (wrap)
        {
            case TextureWrap::Repeat:      return GL_REPEAT;
            case TextureWrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
            default:                       return GL_REPEAT;
        }
    }

    static GLint ToOpenGLMinFilter(const TextureSpecification& spec)
    {
        if (spec.GenerateMipmaps)
        {
            // Respect user's preference for nearest vs linear when mipmaps are enabled.
            return (spec.MinFilter == TextureFilter::Nearest) ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
        }
        return ToOpenGLFilter(spec.MinFilter);
    }

    void OpenGLTexture2D::ApplyParameters() const
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToOpenGLMinFilter(m_Specification));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToOpenGLFilter(m_Specification.MagFilter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToOpenGLWrap(m_Specification.WrapU));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToOpenGLWrap(m_Specification.WrapV));
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path, TextureSpecification specification)
        : m_Specification(specification)
    {
        stbi_set_flip_vertically_on_load(m_Specification.FlipVerticallyOnLoad ? 1 : 0);

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
        if (!data || width <= 0 || height <= 0)
        {
            if (data)
            {
                stbi_image_free(data);
            }
            LT_THROW_RESOURCE_ERROR(std::string("Failed to load texture: ") + path);
        }

        m_Width = static_cast<uint32_t>(width);
        m_Height = static_cast<uint32_t>(height);

        GLenum internalFormat = 0;
        GLenum dataFormat = 0;
        if (channels == 4)
        {
            internalFormat = GL_RGBA8;
            dataFormat = GL_RGBA;
        }
        else if (channels == 3)
        {
            internalFormat = GL_RGB8;
            dataFormat = GL_RGB;
        }
        else
        {
            stbi_image_free(data);
            LT_THROW_RESOURCE_ERROR(
                std::string("Unsupported texture channel count ") + std::to_string(channels) + " for file '" + path + "'");
        }

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        ApplyParameters();

        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                     static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
                     0, dataFormat, GL_UNSIGNED_BYTE, data);

        if (m_Specification.GenerateMipmaps)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        stbi_image_free(data);
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height, const void* rgbaPixels, TextureSpecification specification)
        : m_Width(width)
        , m_Height(height)
        , m_Specification(specification)
    {
        LT_VERIFY(rgbaPixels != nullptr, "RGBA8 pixel data cannot be null");
        LT_VERIFY(width > 0 && height > 0, "Texture size must be non-zero");

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        ApplyParameters();

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);

        if (m_Specification.GenerateMipmaps)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_Width(width)
        , m_Height(height)
    {
        LT_VERIFY(width > 0 && height > 0, "Render target texture size must be non-zero");

        m_Specification.GenerateMipmaps = false;
        m_Specification.MinFilter = TextureFilter::Linear;
        m_Specification.MagFilter = TextureFilter::Linear;
        m_Specification.WrapU = TextureWrap::ClampToEdge;
        m_Specification.WrapV = TextureWrap::ClampToEdge;

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        ApplyParameters();

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    OpenGLTexture2D::OpenGLTexture2D(std::span<const TextureMipLevelRGBA8View> mipLevels, TextureSpecification specification)
        : m_Specification(specification)
    {
        LT_VERIFY(!mipLevels.empty(), "OpenGLTexture2D: mipLevels is empty");
        LT_VERIFY(mipLevels[0].PixelsRGBA8 != nullptr, "OpenGLTexture2D: base mip pixels are null");
        LT_VERIFY(mipLevels[0].Width > 0 && mipLevels[0].Height > 0, "OpenGLTexture2D: base mip size must be non-zero");

        m_Width = mipLevels[0].Width;
        m_Height = mipLevels[0].Height;

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        ApplyParameters();

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(mipLevels.size() - 1u));

        for (size_t level = 0; level < mipLevels.size(); ++level)
        {
            const auto& mip = mipLevels[level];
            LT_VERIFY(mip.PixelsRGBA8 != nullptr, "OpenGLTexture2D: mip pixels are null");
            LT_VERIFY(mip.Width > 0 && mip.Height > 0, "OpenGLTexture2D: mip dimensions must be non-zero");

            glTexImage2D(
                GL_TEXTURE_2D,
                static_cast<GLint>(level),
                GL_RGBA8,
                static_cast<GLsizei>(mip.Width),
                static_cast<GLsizei>(mip.Height),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                mip.PixelsRGBA8);
        }
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint textureToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait("OpenGLTexture/DeleteTexture", [textureToDelete](GraphicsContext*) {
                    GLuint id = textureToDelete;
                    glDeleteTextures(1, &id);
                });
            }
            else
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    GLuint id = textureToDelete;
                    glDeleteTextures(1, &id);
                }
                else
                {
                    LT_CORE_WARN("OpenGLTexture2D destroyed after renderer/context teardown; leaking GL texture {}", textureToDelete);
                }
            }
            m_RendererID = 0;
        }
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    void OpenGLTexture2D::ApplySpecification(const TextureSpecification& specification)
    {
        m_Specification = specification;

        glBindTexture(GL_TEXTURE_2D, m_RendererID);
        ApplyParameters();

        if (m_Specification.GenerateMipmaps)
        {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }
}

