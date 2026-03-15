#pragma once

#include <memory>

namespace Limitless
{
    class GraphicsContext;

    /// Backend-neutral abstraction for graphics context acquisition and
    /// per-frame hooks that differ across rendering APIs.
    ///
    /// The Renderer delegates context management to this interface so that
    /// Renderer.cpp never needs to dynamic_cast or reference API-specific
    /// context types (e.g. OpenGLContext::ScopedCurrentContext).
    class RenderBackendExecutor
    {
    public:
        virtual ~RenderBackendExecutor() = default;

        // ---- Context acquisition --------------------------------------------

        /// RAII guard that makes the graphics context current on construction
        /// and releases it on destruction. Concrete backends provide their own
        /// implementation (e.g. OpenGL ScopedCurrentContext).
        struct ContextScope
        {
            virtual ~ContextScope() = default;
        };

        /// Acquire the graphics context for the calling thread.
        /// The returned guard keeps the context current until destroyed.
        virtual std::unique_ptr<ContextScope> AcquireContext() = 0;

        // ---- Backend hooks --------------------------------------------------

        /// Update backend-specific GPU metrics (VRAM, driver counters, etc.).
        /// Called after ProcessCommands / frame execution while context is current.
        virtual void UpdateGPUMetrics() = 0;

        /// Returns true if this backend supports a dedicated render thread
        /// (i.e. context can be transferred to another thread).
        virtual bool SupportsRenderThread() const = 0;

        // ---- Factory --------------------------------------------------------

        /// Create the appropriate executor for the given context.
        /// Falls back to a generic MakeCurrent()-based executor for unknown backends.
        static std::unique_ptr<RenderBackendExecutor> Create(GraphicsContext* context);
    };
}
