#include "OpenGLCommandExecutor.h"

#include "Graphics/OpenGL/OpenGLContext.h"
#include "Graphics/OpenGL/OpenGLGPUMetrics.h"

namespace Limitless
{
    // ---- OpenGL ContextScope (wraps ScopedCurrentContext) --------------------

    class OpenGLContextScope final : public RenderBackendExecutor::ContextScope
    {
    public:
        explicit OpenGLContextScope(OpenGLContext& context)
            : m_Scope(context)
        {
        }

        ~OpenGLContextScope() override = default;

    private:
        OpenGLContext::ScopedCurrentContext m_Scope;
    };

    // ---- OpenGLCommandExecutor ----------------------------------------------

    OpenGLCommandExecutor::OpenGLCommandExecutor(OpenGLContext* context)
        : m_Context(context)
    {
    }

    std::unique_ptr<RenderBackendExecutor::ContextScope> OpenGLCommandExecutor::AcquireContext()
    {
        if (!m_Context)
            return nullptr;
        return std::make_unique<OpenGLContextScope>(*m_Context);
    }

    void OpenGLCommandExecutor::UpdateGPUMetrics()
    {
        UpdateGPUMetricsFromOpenGL();
    }
}
