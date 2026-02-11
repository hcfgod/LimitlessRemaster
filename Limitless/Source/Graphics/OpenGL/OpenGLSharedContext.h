#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace Limitless
{
    /**
     * OpenGLSharedContext
     *
     * Owns a secondary OpenGL context created with resource sharing enabled
     * (shared object namespace with the primary context).
     *
     * This is intended for **GPU resource work** (uploads/creates/deletes) on a dedicated thread
     * while the primary context continues executing frame rendering/present on the render thread.
     *
     * Important notes:
     * - OpenGL state is per-context; object names/storage are shared across shared contexts.
     * - This is NOT intended for multi-threaded draw execution (that remains single-threaded).
     */
    class OpenGLSharedContext final
    {
    public:
        class ScopedCurrentContext final
        {
        public:
            explicit ScopedCurrentContext(OpenGLSharedContext& context);
            ~ScopedCurrentContext();

            ScopedCurrentContext(const ScopedCurrentContext&) = delete;
            ScopedCurrentContext& operator=(const ScopedCurrentContext&) = delete;

        private:
            OpenGLSharedContext& m_Context;
            std::unique_lock<std::recursive_mutex> m_Lock;
        };

        // Owns both the window and context.
        // The window is typically a hidden "dummy" window used exclusively for this context.
        OpenGLSharedContext(SDL_Window* window, SDL_GLContext sharedContext);
        ~OpenGLSharedContext();

        OpenGLSharedContext(const OpenGLSharedContext&) = delete;
        OpenGLSharedContext& operator=(const OpenGLSharedContext&) = delete;

    private:
        SDL_Window* m_Window = nullptr;
        SDL_GLContext m_Context = nullptr;

        // Context affinity/serialization: only one thread may have THIS context current at a time.
        std::recursive_mutex m_ContextMutex;
        std::thread::id m_CurrentThread{};
        uint32_t m_CurrentDepth = 0;

        friend class ScopedCurrentContext;
    };
}

