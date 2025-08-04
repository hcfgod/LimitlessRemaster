#pragma once

#include "Graphics/GraphicsContext.h"
#include <SDL3/SDL.h>

namespace Limitless {
    class OpenGLContext : public GraphicsContext 
    {
    public:
        OpenGLContext();
        ~OpenGLContext() override;

        // Setup OpenGL attributes with the best supported version
        // This should be called before Init() to ensure optimal version selection
        void SetupAttributes() override;
        
        void MakeCurrent() override;
        void Init(void* nativeWindow, GraphicsAPI api) override;
        void SwapBuffers() override;

        bool SetVSync(bool enabled) override;
        bool IsVSync() const override {
            return m_VSyncActuallyEnabled;
        }

    private:
        SDL_Window* m_Window;
        SDL_GLContext m_Context;
        bool m_VSyncActuallyEnabled{false};

        // Requested GL version
        int m_RequestMajor{4};
        int m_RequestMinor{5};
    };
} // namespace Limitless