#include "ImGuiLayer.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>

// ImGui headers (backend expects imgui before their includes)
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// Vulkan renderer backend - add when Vulkan is implemented:
// #include "imgui/backends/imgui_impl_vulkan.h"

namespace Limitless
{
    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer")
    {
    }

    void ImGuiLayer::OnAttach()
    {
        auto& app = Application::GetInstance();
        Window& window = app.GetWindow();
        GraphicsContext* context = window.GetGraphicsContext();

        if (!context)
        {
            LT_CORE_ERROR("ImGuiLayer: No graphics context available.");
            return;
        }

        m_GraphicsAPI = context->GetAPI();

        switch (m_GraphicsAPI)
        {
            case GraphicsAPI::OpenGL:
                OnAttachOpenGL(window, context);
                break;
            case GraphicsAPI::Vulkan:
                LT_CORE_ERROR("ImGuiLayer: Vulkan backend not yet implemented. Add imgui_impl_vulkan and wire it up.");
                return;
            case GraphicsAPI::DirectX:
                LT_CORE_ERROR("ImGuiLayer: DirectX backend not yet implemented.");
                return;
            case GraphicsAPI::Metal:
                LT_CORE_ERROR("ImGuiLayer: Metal backend not yet implemented.");
                return;
            default:
                LT_CORE_ERROR("ImGuiLayer: Unknown graphics API.");
                return;
        }
    }

    void ImGuiLayer::OnAttachOpenGL(Window& window, GraphicsContext* context)
    {
        if (!context->GetNativeContext())
        {
            LT_CORE_ERROR("ImGuiLayer: OpenGL context has no native handle.");
            return;
        }

        SDL_Window* sdlWindow = static_cast<SDL_Window*>(window.GetNativeWindow());
        void* glContext = context->GetNativeContext();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // Docking enabled by default
        // Persist layout (docking, window positions) to imgui.ini in working directory.
        io.IniFilename = "imgui.ini";

        if (!ImGui_ImplSDL3_InitForOpenGL(sdlWindow, glContext))
        {
            LT_CORE_ERROR("ImGuiLayer: ImGui_ImplSDL3_InitForOpenGL failed.");
            ImGui::DestroyContext();
            return;
        }

        OpenGLContext* glContextObj = dynamic_cast<OpenGLContext*>(context);
        if (!glContextObj)
        {
            LT_CORE_ERROR("ImGuiLayer: Expected OpenGL context.");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        {
            OpenGLContext::ScopedCurrentContext scope(*glContextObj);
            if (!ImGui_ImplOpenGL3_Init("#version 150"))
            {
                LT_CORE_ERROR("ImGuiLayer: ImGui_ImplOpenGL3_Init failed.");
                ImGui_ImplSDL3_Shutdown();
                ImGui::DestroyContext();
                return;
            }
        }

        m_Initialized = true;

        SDLWindow* sdlWindowObj = dynamic_cast<SDLWindow*>(&window);
        if (sdlWindowObj)
        {
            sdlWindowObj->SetSdlEventCallback([](const SDL_Event& event) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            });
        }

        Application::GetInstance().SetImGuiCallbacks(
            [this]() { BeginFrame(); },
            [this]() { EndFrame(); }
        );

        LT_CORE_INFO("ImGuiLayer attached successfully (OpenGL backend)");
    }

    void ImGuiLayer::OnDetach()
    {
        if (!m_Initialized)
            return;

        auto& app = Application::GetInstance();
        app.SetImGuiCallbacks(nullptr, nullptr);

        SDLWindow* sdlWindowObj = dynamic_cast<SDLWindow*>(&app.GetWindow());
        if (sdlWindowObj)
            sdlWindowObj->SetSdlEventCallback(nullptr);

        switch (m_GraphicsAPI)
        {
            case GraphicsAPI::OpenGL:
                ImGui_ImplOpenGL3_Shutdown();
                ImGui_ImplSDL3_Shutdown();
                break;
            case GraphicsAPI::Vulkan:
                // ImGui_ImplVulkan_Shutdown();
                break;
            case GraphicsAPI::DirectX:
            case GraphicsAPI::Metal:
                break;
            default:
                break;
        }

        ImGui::DestroyContext();
        m_Initialized = false;
        LT_CORE_INFO("ImGuiLayer detached");
    }

    void ImGuiLayer::BeginFrame()
    {
        if (!m_Initialized)
            return;

        ImGui_ImplSDL3_NewFrame();

        switch (m_GraphicsAPI)
        {
            case GraphicsAPI::OpenGL:
            {
                auto& app = Application::GetInstance();
                if (auto* glContext = dynamic_cast<OpenGLContext*>(app.GetWindow().GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    ImGui_ImplOpenGL3_NewFrame();
                }
                else
                {
                    ImGui_ImplOpenGL3_NewFrame();
                }
                break;
            }
            case GraphicsAPI::Vulkan:
                // ImGui_ImplVulkan_NewFrame();
                break;
            case GraphicsAPI::DirectX:
            case GraphicsAPI::Metal:
                break;
            default:
                break;
        }

        ImGui::NewFrame();
    }

    void ImGuiLayer::EndFrame()
    {
        if (!m_Initialized)
            return;

        ImGui::Render();

        switch (m_GraphicsAPI)
        {
            case GraphicsAPI::OpenGL:
            {
                auto& app = Application::GetInstance();
                if (auto* glContext = dynamic_cast<OpenGLContext*>(app.GetWindow().GetGraphicsContext()))
                {
                    OpenGLContext::ScopedCurrentContext scope(*glContext);
                    // Ensure we're on the default framebuffer before clear (avoids GL_INVALID_OPERATION
                    // when ProcessCommands left a custom framebuffer bound).
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    // Clear the framebuffer before drawing. The Editor has no game layer that clears;
                    // without this, previous frames persist and cause ghosting when moving the window.
                    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                }
                else
                {
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                }
                break;
            }
            case GraphicsAPI::Vulkan:
                // ImGui_ImplVulkan_RenderDrawData(...);
                break;
            case GraphicsAPI::DirectX:
            case GraphicsAPI::Metal:
                break;
            default:
                break;
        }
    }

    void ImGuiLayer::OnRender()
    {
        if (!m_Initialized)
            return;

        // Default dockspace fills the viewport; windows can be docked into it.
        if (m_ShowDockspace)
        {
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        }

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

}  // namespace Limitless
