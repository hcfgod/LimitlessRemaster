#pragma once

#include "Graphics/Shader.h"

#define LT_USE_GLAD
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

        void SetInt(const std::string& name, int value) override;
        void SetMat4(const std::string& name, const glm::mat4& value) override;

        const std::string& GetName() const override { return m_Name; }

        GLuint GetRendererID() const { return m_RendererID; }

    private:
        static GLuint CompileShader(GLenum type, const std::string& source);

        GLuint m_RendererID = 0;
        std::string m_Name;
    };
}

