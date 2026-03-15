#pragma once

#include <memory>

namespace Limitless
{
    /// Backend-neutral interface for a secondary graphics context used by the
    /// resource thread to perform GPU resource work (uploads, creates, deletes)
    /// in parallel with frame rendering on the primary context.
    ///
    /// Each backend provides a concrete implementation:
    /// - OpenGL: wraps a shared GL context (shared object namespace).
    /// - Vulkan/DX/Metal: may be a no-op or use device-level thread-safety.
    ///
    /// The resource thread calls MakeCurrent() before executing resource work
    /// and ReleaseCurrent() when done.
    class SharedResourceContext
    {
    public:
        virtual ~SharedResourceContext() = default;

        /// Make this secondary context current on the calling thread.
        /// Must be paired with ReleaseCurrent().
        virtual void MakeCurrent() = 0;

        /// Release the context from the calling thread.
        virtual void ReleaseCurrent() = 0;

        /// RAII helper for scoped context activation.
        class ScopedCurrent final
        {
        public:
            explicit ScopedCurrent(SharedResourceContext& context)
                : m_Context(context)
            {
                m_Context.MakeCurrent();
            }

            ~ScopedCurrent()
            {
                m_Context.ReleaseCurrent();
            }

            ScopedCurrent(const ScopedCurrent&) = delete;
            ScopedCurrent& operator=(const ScopedCurrent&) = delete;

        private:
            SharedResourceContext& m_Context;
        };
    };

}  // namespace Limitless
