#include "RenderCommandExecutor.h"

#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include "Graphics/OpenGL/OpenGLCommandExecutor.h"

namespace Limitless
{
    // ---- Generic fallback executor (MakeCurrent-based) ----------------------

    class GenericCommandExecutor final : public RenderBackendExecutor
    {
    public:
        explicit GenericCommandExecutor(GraphicsContext* context)
            : m_Context(context)
        {
        }

        std::unique_ptr<ContextScope> AcquireContext() override
        {
            if (m_Context)
                m_Context->MakeCurrent();
            return nullptr; // no RAII guard needed; MakeCurrent is sticky
        }

        void UpdateGPUMetrics() override
        {
            // No generic GPU metrics available for unknown backends.
        }

        bool SupportsRenderThread() const override { return false; }

    private:
        GraphicsContext* m_Context = nullptr;
    };

    // ---- Factory ------------------------------------------------------------

    std::unique_ptr<RenderBackendExecutor> RenderBackendExecutor::Create(GraphicsContext* context)
    {
        if (!context)
            return nullptr;

        if (auto* glContext = dynamic_cast<OpenGLContext*>(context))
        {
            return std::make_unique<OpenGLCommandExecutor>(glContext);
        }

        return std::make_unique<GenericCommandExecutor>(context);
    }
}
