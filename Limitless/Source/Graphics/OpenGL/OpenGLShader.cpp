#include "OpenGLShader.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

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
                renderer.SubmitResourceAndWait([programToDelete](GraphicsContext*) {
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
            renderer.SubmitResourceAndWait([&](GraphicsContext*) {
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
}

