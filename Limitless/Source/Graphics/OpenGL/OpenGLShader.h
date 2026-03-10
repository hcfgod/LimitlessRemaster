#pragma once

#include "Graphics/Shader.h"

#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace Limitless
{
    class OpenGLShader final : public Shader
    {
    public:
        OpenGLShader(std::string name, const std::string& vertexSrc, const std::string& fragmentSrc);
        ~OpenGLShader() override;

        void Bind() const override;
        void Unbind() const override;

        const std::string& GetName() const override { return m_Name; }
        uintptr_t GetNativeHandle() const override { return static_cast<uintptr_t>(m_RendererID); }

    private:
        static GLuint CompileShader(GLenum type, const std::string& source);

        GLuint m_RendererID = 0;
        std::string m_Name;
    };
}

