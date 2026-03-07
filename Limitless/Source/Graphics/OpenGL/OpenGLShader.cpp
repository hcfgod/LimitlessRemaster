#include "OpenGLShader.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <glm/gtc/type_ptr.hpp>

namespace Limitless
{
    static GLint GetUniformLocation(GLuint program, const std::string& name)
    {
        GLint location = glGetUniformLocation(program, name.c_str());
        if (location == -1)
        {
            // Not all shaders use all uniforms; keep this as a debug hint, not a hard error.
            LT_CORE_DEBUG("OpenGLShader: uniform '{}' not found in program {}", name, program);
        }
        return location;
    }

    static GLint GetUniformLocationWithArrayFallback(GLuint program, const std::string& name)
    {
        GLint location = glGetUniformLocation(program, name.c_str());
        if (location != -1)
        {
            return location;
        }

        // For uniform arrays, OpenGL commonly expects the base element syntax: "u_Textures[0]".
        const std::string withZero = name + "[0]";
        location = glGetUniformLocation(program, withZero.c_str());
        if (location == -1)
        {
            LT_CORE_DEBUG("OpenGLShader: uniform '{}' (or '{}') not found in program {}", name, withZero, program);
        }
        return location;
    }

    GLuint OpenGLShader::CompileShader(GLenum type, const std::string& source)
    {
        const char* src = source.c_str();
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        GLint compiled = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_FALSE)
        {
            GLint length = 0;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
            std::string infoLog;
            infoLog.resize(static_cast<size_t>(length));
            glGetShaderInfoLog(id, length, &length, infoLog.data());

            glDeleteShader(id);
            LT_THROW_GRAPHICS_ERROR(infoLog);
        }

        return id;
    }

    OpenGLShader::OpenGLShader(std::string name, const std::string& vertexSrc, const std::string& fragmentSrc)
        : m_Name(std::move(name))
    {
        GLuint program = glCreateProgram();

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string infoLog;
            infoLog.resize(static_cast<size_t>(length));
            glGetProgramInfoLog(program, length, &length, infoLog.data());

            glDeleteProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            LT_THROW_GRAPHICS_ERROR(infoLog);
        }

        glDetachShader(program, vs);
        glDetachShader(program, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_RendererID = program;
    }

    OpenGLShader::~OpenGLShader()
    {
        if (m_RendererID)
        {
            auto& renderer = Renderer::GetInstance();
            const GLuint programToDelete = m_RendererID;
            if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
            {
                renderer.SubmitResourceAndWait("OpenGLShader/DeleteProgram", [programToDelete](GraphicsContext*) {
                    glDeleteProgram(programToDelete);
                });
            }
            else
            {
                // Enforce: never call glDelete* without a valid current context.
                if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    glDeleteProgram(programToDelete);
                }
                else
                {
                    LT_CORE_WARN("OpenGLShader '{}' destroyed after renderer/context teardown; leaking GL program {}", m_Name, programToDelete);
                }
            }
            m_RendererID = 0;
        }
    }

    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetInt(const std::string& name, int value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetInt", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniform1i(location, value);
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniform1i(location, value);
        }
    }

    void OpenGLShader::SetIntArray(const std::string& name, const int* values, uint32_t count)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetIntArray", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
                if (location != -1)
                {
                    glUniform1iv(location, static_cast<GLsizei>(count), values);
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
        if (location != -1)
        {
            glUniform1iv(location, static_cast<GLsizei>(count), values);
        }
    }

    void OpenGLShader::SetFloat(const std::string& name, float value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniform1f(location, value);
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniform1f(location, value);
        }
    }

    void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat2", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniform2fv(location, 1, glm::value_ptr(value));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniform2fv(location, 1, glm::value_ptr(value));
        }
    }

    void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat3", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniform3fv(location, 1, glm::value_ptr(value));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniform3fv(location, 1, glm::value_ptr(value));
        }
    }

    void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat4", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniform4fv(location, 1, glm::value_ptr(value));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniform4fv(location, 1, glm::value_ptr(value));
        }
    }

    void OpenGLShader::SetFloat2Array(const std::string& name, const glm::vec2* values, uint32_t count)
    {
        if (values == nullptr || count == 0)
        {
            return;
        }

        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat2Array", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
                if (location != -1)
                {
                    glUniform2fv(location, static_cast<GLsizei>(count), glm::value_ptr(values[0]));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
        if (location != -1)
        {
            glUniform2fv(location, static_cast<GLsizei>(count), glm::value_ptr(values[0]));
        }
    }

    void OpenGLShader::SetFloat4Array(const std::string& name, const glm::vec4* values, uint32_t count)
    {
        if (values == nullptr || count == 0)
        {
            return;
        }

        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetFloat4Array", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
                if (location != -1)
                {
                    glUniform4fv(location, static_cast<GLsizei>(count), glm::value_ptr(values[0]));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocationWithArrayFallback(m_RendererID, name);
        if (location != -1)
        {
            glUniform4fv(location, static_cast<GLsizei>(count), glm::value_ptr(values[0]));
        }
    }

    void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
    {
        auto& renderer = Renderer::GetInstance();
        if (renderer.IsInitialized() && renderer.IsRenderThreadEnabled() && renderer.GetGraphicsContext() != nullptr)
        {
            renderer.SubmitResourceAndWait("OpenGLShader/SetMat4", [&](GraphicsContext*) {
                glUseProgram(m_RendererID);
                GLint location = GetUniformLocation(m_RendererID, name);
                if (location != -1)
                {
                    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
                }
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(renderer.GetGraphicsContext()))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
        }

        glUseProgram(m_RendererID);
        GLint location = GetUniformLocation(m_RendererID, name);
        if (location != -1)
        {
            glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
        }
    }
}

