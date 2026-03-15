#pragma once

#include "Graphics/SharedResourceContext.h"
#include "Graphics/OpenGL/OpenGLSharedContext.h"

#include <memory>
#include <optional>

namespace Limitless
{
    /// OpenGL implementation of SharedResourceContext.
    ///
    /// Wraps an OpenGLSharedContext (shared GL object namespace) and delegates
    /// MakeCurrent/ReleaseCurrent to OpenGLSharedContext::ScopedCurrentContext.
    class OpenGLSharedResourceContext final : public SharedResourceContext
    {
    public:
        explicit OpenGLSharedResourceContext(std::unique_ptr<OpenGLSharedContext> glSharedContext);
        ~OpenGLSharedResourceContext() override;

        OpenGLSharedResourceContext(const OpenGLSharedResourceContext&) = delete;
        OpenGLSharedResourceContext& operator=(const OpenGLSharedResourceContext&) = delete;

        void MakeCurrent() override;
        void ReleaseCurrent() override;

    private:
        std::unique_ptr<OpenGLSharedContext> m_GLSharedContext;
        std::optional<OpenGLSharedContext::ScopedCurrentContext> m_ScopedContext;
    };

}  // namespace Limitless
