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

        void SetInt(const std::string& name, int value) override;
        void SetIntArray(const std::string& name, const int* values, uint32_t count) override;
        void SetFloat(const std::string& name, float value) override;
        void SetFloat2(const std::string& name, const glm::vec2& value) override;
        void SetFloat3(const std::string& name, const glm::vec3& value) override;
        void SetFloat4(const std::string& name, const glm::vec4& value) override;
        void SetFloat2Array(const std::string& name, const glm::vec2* values, uint32_t count) override;
        void SetFloat4Array(const std::string& name, const glm::vec4* values, uint32_t count) override;
        void SetMat4(const std::string& name, const glm::mat4& value) override;

        const std::string& GetName() const override { return m_Name; }
        uintptr_t GetNativeHandle() const override { return static_cast<uintptr_t>(m_RendererID); }

    private:
        static GLuint CompileShader(GLenum type, const std::string& source);

        GLuint m_RendererID = 0;
        std::string m_Name;
    };
}

