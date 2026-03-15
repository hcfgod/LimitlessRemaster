#include "OpenGLSharedResourceContext.h"

namespace Limitless
{
    OpenGLSharedResourceContext::OpenGLSharedResourceContext(std::unique_ptr<OpenGLSharedContext> glSharedContext)
        : m_GLSharedContext(std::move(glSharedContext))
    {
    }

    OpenGLSharedResourceContext::~OpenGLSharedResourceContext()
    {
        // Ensure context is released before the shared context is destroyed.
        ReleaseCurrent();
    }

    void OpenGLSharedResourceContext::MakeCurrent()
    {
        if (m_GLSharedContext && !m_ScopedContext.has_value())
        {
            m_ScopedContext.emplace(*m_GLSharedContext);
        }
    }

    void OpenGLSharedResourceContext::ReleaseCurrent()
    {
        m_ScopedContext.reset();
    }

}  // namespace Limitless
