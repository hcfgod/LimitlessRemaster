#pragma once

#include "Graphics/RenderCommandExecutor.h"

namespace Limitless
{
    class OpenGLContext;

    class OpenGLCommandExecutor final : public RenderBackendExecutor
    {
    public:
        explicit OpenGLCommandExecutor(OpenGLContext* context);
        ~OpenGLCommandExecutor() override = default;

        // ---- Context acquisition --------------------------------------------
        std::unique_ptr<ContextScope> AcquireContext() override;

        // ---- Backend hooks --------------------------------------------------
        void UpdateGPUMetrics() override;
        bool SupportsRenderThread() const override { return true; }

    private:
        OpenGLContext* m_Context = nullptr; // borrowed, not owned
    };
}
