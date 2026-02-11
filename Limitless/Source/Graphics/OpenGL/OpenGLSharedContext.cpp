#include "Graphics/OpenGL/OpenGLSharedContext.h"

#include "Core/Debug/Log.h"

#include <stdexcept>

namespace Limitless
{
    OpenGLSharedContext::ScopedCurrentContext::ScopedCurrentContext(OpenGLSharedContext& context)
        : m_Context(context)
        , m_Lock(context.m_ContextMutex)
    {
        const std::thread::id thisThread = std::this_thread::get_id();

        // Re-entrant behavior for convenience.
        if (m_Context.m_CurrentDepth > 0 && m_Context.m_CurrentThread == thisThread)
        {
            m_Context.m_CurrentDepth++;
            return;
        }

        // SDL3 returns true on success, false on failure.
        if (!SDL_GL_MakeCurrent(m_Context.m_Window, m_Context.m_Context))
        {
            LT_CORE_CRITICAL("OpenGLSharedContext: could not make shared GL context current: {}", SDL_GetError());
            throw std::runtime_error("OpenGLSharedContext: SDL_GL_MakeCurrent failed");
        }

        m_Context.m_CurrentThread = thisThread;
        m_Context.m_CurrentDepth = 1;
    }

    OpenGLSharedContext::ScopedCurrentContext::~ScopedCurrentContext()
    {
        if (m_Context.m_CurrentDepth == 0)
        {
            return;
        }

        m_Context.m_CurrentDepth--;
        if (m_Context.m_CurrentDepth == 0)
        {
            (void)SDL_GL_MakeCurrent(m_Context.m_Window, nullptr);
            m_Context.m_CurrentThread = std::thread::id{};
        }
    }

    OpenGLSharedContext::OpenGLSharedContext(SDL_Window* window, SDL_GLContext sharedContext)
        : m_Window(window)
        , m_Context(sharedContext)
    {
        if (!m_Window || !m_Context)
        {
            throw std::runtime_error("OpenGLSharedContext: invalid (window/context) arguments");
        }
    }

    OpenGLSharedContext::~OpenGLSharedContext()
    {
        // Best-effort clear current context before destruction.
        if (m_Window)
        {
            (void)SDL_GL_MakeCurrent(m_Window, nullptr);
        }

        if (m_Context)
        {
            LT_CORE_INFO("OpenGLSharedContext: destroying shared OpenGL context");
            SDL_GL_DestroyContext(m_Context);
            m_Context = nullptr;
        }

        if (m_Window)
        {
            LT_CORE_INFO("OpenGLSharedContext: destroying shared context window");
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }
    }
}

