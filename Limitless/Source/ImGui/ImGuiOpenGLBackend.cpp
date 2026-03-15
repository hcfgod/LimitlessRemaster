#include "ImGuiOpenGLBackend.h"

#include "Core/Debug/Log.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLContext.h"

#include <glad/glad.h>
#include <SDL3/SDL.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

namespace Limitless
{
    bool ImGuiOpenGLBackend::Init(Window& window, GraphicsContext* context)
    {
        if (!context || !context->GetNativeContext())
        {
            LT_CORE_ERROR("ImGuiOpenGLBackend: context is null or has no native handle.");
            return false;
        }

        SDL_Window* sdlWindow = static_cast<SDL_Window*>(window.GetNativeWindow());
        void* glContext = context->GetNativeContext();

        if (!ImGui_ImplSDL3_InitForOpenGL(sdlWindow, glContext))
        {
            LT_CORE_ERROR("ImGuiOpenGLBackend: ImGui_ImplSDL3_InitForOpenGL failed.");
            return false;
        }

        auto* glContextObj = dynamic_cast<OpenGLContext*>(context);
        if (!glContextObj)
        {
            LT_CORE_ERROR("ImGuiOpenGLBackend: Expected OpenGL context.");
            ImGui_ImplSDL3_Shutdown();
            return false;
        }

        {
            OpenGLContext::ScopedCurrentContext scope(*glContextObj);
            if (!ImGui_ImplOpenGL3_Init("#version 150"))
            {
                LT_CORE_ERROR("ImGuiOpenGLBackend: ImGui_ImplOpenGL3_Init failed.");
                ImGui_ImplSDL3_Shutdown();
                return false;
            }
        }

        m_GLContext = glContextObj;
        return true;
    }

    void ImGuiOpenGLBackend::Shutdown()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        m_GLContext = nullptr;
    }

    void ImGuiOpenGLBackend::NewFrame()
    {
        if (m_GLContext)
        {
            OpenGLContext::ScopedCurrentContext scope(*m_GLContext);
            ImGui_ImplOpenGL3_NewFrame();
        }
        else
        {
            ImGui_ImplOpenGL3_NewFrame();
        }
    }

    void ImGuiOpenGLBackend::RenderDrawData(ImDrawData* drawData)
    {
        if (m_GLContext)
        {
            OpenGLContext::ScopedCurrentContext scope(*m_GLContext);
            // Ensure we're on the default framebuffer before clear (avoids GL_INVALID_OPERATION
            // when ProcessCommands left a custom framebuffer bound).
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            // Clear the framebuffer before drawing. The Editor has no game layer that clears;
            // without this, previous frames persist and cause ghosting when moving the window.
            glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }
        else
        {
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }
    }

}  // namespace Limitless
