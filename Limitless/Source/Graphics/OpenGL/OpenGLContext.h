#pragma once

#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLSharedContext.h"
#include <SDL3/SDL.h>
#include <mutex>
#include <thread>
#include <memory>

namespace Limitless {
    class OpenGLContext : public GraphicsContext 
    {
    public:
        class ScopedCurrentContext final
        {
        public:
            explicit ScopedCurrentContext(OpenGLContext& context);
            ~ScopedCurrentContext();

            ScopedCurrentContext(const ScopedCurrentContext&) = delete;
            ScopedCurrentContext& operator=(const ScopedCurrentContext&) = delete;

        private:
            OpenGLContext& m_Context;
            std::unique_lock<std::recursive_mutex> m_Lock;
        };

        OpenGLContext();
        ~OpenGLContext() override;

        // Setup OpenGL attributes with the best supported version
        // This should be called before Init() to ensure optimal version selection
        void SetupAttributes() override;
        
        void MakeCurrent() override;
        void Init(void* nativeWindow, GraphicsAPI api) override;
        void SwapBuffers() override;
        void SetViewport(int x, int y, int width, int height) override;
        void* GetNativeContext() override { return m_Context; }
        GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }

        bool SetVSync(bool enabled) override;
        bool IsVSync() const override {
            return m_VSyncActuallyEnabled;
        }

        bool IsCurrentOnThisThread() const override {
            return m_CurrentDepth > 0 && m_CurrentThread == std::this_thread::get_id();
        }

        int32_t GetMaxTextureImageUnits() const override { return m_MaxTextureImageUnits; }

        // Create a resource-sharing OpenGL context suitable for running GPU resource work on a
        // secondary thread (uploads/creates/deletes). Returns null if creation fails.
        //
        // Notes:
        // - The created context shares objects with the primary context.
        // - This is NOT intended for multi-threaded draw execution.
        std::unique_ptr<OpenGLSharedContext> CreateSharedContext();

    private:
        SDL_Window* m_Window;
        SDL_GLContext m_Context;
        bool m_VSyncActuallyEnabled{false};

        // Context affinity/serialization for OpenGL:
        // OpenGL contexts are thread-affine; only one thread may have the context current at a time.
        // We serialize context usage with this mutex and explicitly make the context current in a scope.
        std::recursive_mutex m_ContextMutex;
        std::thread::id m_CurrentThread{};
        uint32_t m_CurrentDepth = 0;

        // Requested GL version
        int m_RequestMajor{4};
        int m_RequestMinor{5};

        int32_t m_MaxTextureImageUnits{16};
    };
} // namespace Limitless