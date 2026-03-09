#include "ImGuiLayer.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"
#include "Platform/Platform.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <filesystem>

#include <cmath>
#include <system_error>

// ImGui headers (backend expects imgui before their includes)
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"

// Vulkan renderer backend - add when Vulkan is implemented:
// #include "imgui/backends/imgui_impl_vulkan.h"

namespace Limitless
{
    namespace
    {
        ImGuiID GetMainViewportDockspaceId(const ImGuiViewport* viewport)
        {
            if (viewport == nullptr)
                return 0;

            char label[32];
            ImFormatString(label, IM_ARRAYSIZE(label), "WindowOverViewport_%08X", viewport->ID);
            return ImHashStr("DockSpace", 0, ImHashStr(label));
        }

        float GetDockNodeAxisLength(const ImGuiDockNode* node, ImGuiAxis axis)
        {
            if (node == nullptr)
                return 0.0f;

            const float size = node->Size[axis];
            if (size > 0.0f)
                return size;

            return node->SizeRef[axis];
        }

        void CaptureDockNodeSplitRatios(const ImGuiDockNode* node, std::unordered_map<unsigned int, float>& splitRatios)
        {
            if (node == nullptr || !node->IsSplitNode())
                return;

            const ImGuiAxis splitAxis = static_cast<ImGuiAxis>(node->SplitAxis);
            if (splitAxis != ImGuiAxis_X && splitAxis != ImGuiAxis_Y)
                return;

            const ImGuiDockNode* child0 = node->ChildNodes[0];
            const ImGuiDockNode* child1 = node->ChildNodes[1];
            if (child0 == nullptr || child1 == nullptr)
                return;

            const float size0 = GetDockNodeAxisLength(child0, splitAxis);
            const float size1 = GetDockNodeAxisLength(child1, splitAxis);
            const float totalSize = ImMax(size0 + size1, 1.0f);
            splitRatios[node->ID] = ImClamp(size0 / totalSize, 0.05f, 0.95f);

            CaptureDockNodeSplitRatios(child0, splitRatios);
            CaptureDockNodeSplitRatios(child1, splitRatios);
        }

        void ProjectDockNodeSizeRefs(ImGuiDockNode* node,
                                     const ImVec2& targetSize,
                                     const std::unordered_map<unsigned int, float>& splitRatios)
        {
            if (node == nullptr || !node->IsSplitNode())
                return;

            const ImGuiAxis splitAxis = static_cast<ImGuiAxis>(node->SplitAxis);
            if (splitAxis != ImGuiAxis_X && splitAxis != ImGuiAxis_Y)
                return;

            ImGuiDockNode* child0 = node->ChildNodes[0];
            ImGuiDockNode* child1 = node->ChildNodes[1];
            if (child0 == nullptr || child1 == nullptr)
                return;

            ImGuiStyle& style = ImGui::GetStyle();
            const float spacing = style.DockingSeparatorSize;
            const float currentSize0 = GetDockNodeAxisLength(child0, splitAxis);
            const float currentSize1 = GetDockNodeAxisLength(child1, splitAxis);
            const float currentAvailable = ImMax(currentSize0 + currentSize1, 1.0f);
            const float targetAvailable = ImMax(targetSize[splitAxis] - spacing, 1.0f);
            const float targetMinEach = ImTrunc(ImMin(targetAvailable, style.WindowMinSize[splitAxis] * 2.0f) * 0.5f);
            const bool child0HasCentralContent = child0->HasCentralNodeChild || child0->IsCentralNode();
            const bool child1HasCentralContent = child1->HasCentralNodeChild || child1->IsCentralNode();

            float splitRatio = currentSize0 / currentAvailable;
            if (const auto ratioIt = splitRatios.find(node->ID); ratioIt != splitRatios.end())
                splitRatio = ratioIt->second;
            splitRatio = ImClamp(splitRatio, 0.05f, 0.95f);

            float targetSize0 = ImTrunc(targetAvailable * splitRatio + 0.5f);
            if (targetAvailable >= targetMinEach * 2.0f)
                targetSize0 = ImClamp(targetSize0, targetMinEach, targetAvailable - targetMinEach);
            else
                targetSize0 = ImClamp(targetSize0, 1.0f, targetAvailable - 1.0f);

            float targetSize1 = ImMax(1.0f, targetAvailable - targetSize0);

            if (splitAxis == ImGuiAxis_X && child0HasCentralContent && !child1HasCentralContent)
            {
                const float maxPeripheralSize = ImMax(1.0f, targetAvailable - targetMinEach);
                const float minPeripheralSize = ImMin(maxPeripheralSize, ImMax(targetMinEach, ImMax(400.0f, ImTrunc(targetAvailable * 0.45f))));
                targetSize1 = ImClamp(ImMax(targetSize1, minPeripheralSize), 1.0f, targetAvailable - 1.0f);
                targetSize0 = ImMax(1.0f, targetAvailable - targetSize1);

                const float maxCentralSize = ImMax(1.0f, targetAvailable - ImMax(targetMinEach, ImTrunc(targetAvailable * 0.45f)));
                if (targetSize0 > maxCentralSize)
                {
                    targetSize0 = maxCentralSize;
                    targetSize1 = ImMax(1.0f, targetAvailable - targetSize0);
                }
            }

            child0->SizeRef[splitAxis] = targetSize0;
            child1->SizeRef[splitAxis] = targetSize1;

            ImVec2 child0TargetSize = targetSize;
            ImVec2 child1TargetSize = targetSize;
            child0TargetSize[splitAxis] = targetSize0;
            child1TargetSize[splitAxis] = targetSize1;

            ProjectDockNodeSizeRefs(child0, child0TargetSize, splitRatios);
            ProjectDockNodeSizeRefs(child1, child1TargetSize, splitRatios);
        }

        std::filesystem::path ResolveImGuiLayoutDirectory()
        {
            const std::filesystem::path executablePath = PlatformDetection::GetExecutablePath();
            if (!executablePath.empty() && executablePath.has_parent_path())
                return executablePath.parent_path();

            std::error_code errorCode;
            const std::filesystem::path workingDirectory = std::filesystem::current_path(errorCode);
            if (!errorCode)
                return workingDirectory;

            return {};
        }

        std::filesystem::path ResolveDefaultLayoutPath(const std::filesystem::path& layoutDirectory)
        {
            std::error_code errorCode;
            const std::filesystem::path directDefaultPath = layoutDirectory / "imgui-default.ini";
            if (std::filesystem::exists(directDefaultPath, errorCode))
                return directDefaultPath;

            // Dev fallback: walk parents and probe "<parent>/Editor/imgui-default.ini".
            std::filesystem::path probe = layoutDirectory;
            for (int depth = 0; depth < 8 && !probe.empty(); ++depth)
            {
                errorCode.clear();
                const std::filesystem::path candidate = probe / "Editor" / "imgui-default.ini";
                if (std::filesystem::exists(candidate, errorCode))
                    return candidate;

                const std::filesystem::path parent = probe.parent_path();
                if (parent == probe)
                    break;
                probe = parent;
            }

            return {};
        }

        void EnsureActiveLayoutExists(const std::filesystem::path& activeLayoutPath,
                                      const std::filesystem::path& defaultLayoutPath)
        {
            std::error_code errorCode;
            if (defaultLayoutPath.empty() || !std::filesystem::exists(defaultLayoutPath, errorCode))
                return;
            // Only seed from default when active layout does not exist, so user docking/position
            // and panel visibility (e.g. Project, Performance) are preserved across sessions.
            if (std::filesystem::exists(activeLayoutPath, errorCode) && !errorCode)
                return;

            std::filesystem::create_directories(activeLayoutPath.parent_path(), errorCode);
            errorCode.clear();
            std::filesystem::copy_file(
                defaultLayoutPath,
                activeLayoutPath,
                std::filesystem::copy_options::overwrite_existing,
                errorCode);

            if (!errorCode)
                LT_CORE_INFO("ImGui layout loaded from '{}'.", defaultLayoutPath.string());
        }

        void ApplyModernEditorImGuiTheme()
        {
            ImGuiStyle& style = ImGui::GetStyle();

            // Softer spacing/rounding than default ImGui for a more modern editor feel.
            style.WindowPadding = ImVec2(10.0f, 10.0f);
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.CellPadding = ImVec2(8.0f, 4.0f);
            style.ItemSpacing = ImVec2(8.0f, 6.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
            style.IndentSpacing = 20.0f;
            style.ScrollbarSize = 12.0f;
            style.GrabMinSize = 8.0f;

            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.TabBorderSize = 0.0f;

            style.WindowRounding = 8.0f;
            style.ChildRounding = 8.0f;
            style.FrameRounding = 6.0f;
            style.PopupRounding = 6.0f;
            style.ScrollbarRounding = 10.0f;
            style.GrabRounding = 6.0f;
            style.TabRounding = 6.0f;

            ImVec4* colors = style.Colors;
            colors[ImGuiCol_Text] = ImVec4(0.91f, 0.94f, 0.98f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.58f, 0.66f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.09f, 0.12f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.11f, 0.15f, 0.85f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.97f);
            colors[ImGuiCol_Border] = ImVec4(0.22f, 0.26f, 0.33f, 0.70f);
            colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

            colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.17f, 0.23f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.29f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.27f, 0.35f, 1.00f);

            colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.11f, 0.15f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.14f, 0.19f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.11f, 0.15f, 0.85f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.13f, 0.18f, 1.00f);

            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.80f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.33f, 0.43f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.40f, 0.52f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.37f, 0.45f, 0.58f, 1.00f);

            colors[ImGuiCol_CheckMark] = ImVec4(0.38f, 0.68f, 1.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.36f, 0.65f, 0.98f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.46f, 0.75f, 1.00f, 1.00f);

            colors[ImGuiCol_Button] = ImVec4(0.19f, 0.24f, 0.31f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.31f, 0.40f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.37f, 0.49f, 1.00f);

            colors[ImGuiCol_Header] = ImVec4(0.18f, 0.23f, 0.31f, 0.85f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.30f, 0.40f, 0.90f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.37f, 0.49f, 0.95f);

            colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.29f, 0.36f, 0.75f);
            colors[ImGuiCol_SeparatorHovered] = ImVec4(0.36f, 0.52f, 0.77f, 0.85f);
            colors[ImGuiCol_SeparatorActive] = ImVec4(0.43f, 0.63f, 0.92f, 0.95f);

            colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.32f, 0.40f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.36f, 0.52f, 0.77f, 0.78f);
            colors[ImGuiCol_ResizeGripActive] = ImVec4(0.43f, 0.63f, 0.92f, 0.92f);

            colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.16f, 0.22f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.34f, 0.47f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.21f, 0.30f, 0.42f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.14f, 0.19f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.22f, 0.31f, 1.00f);

            colors[ImGuiCol_DockingPreview] = ImVec4(0.36f, 0.62f, 0.93f, 0.55f);
            colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.07f, 0.09f, 0.12f, 1.00f);

            colors[ImGuiCol_TableHeaderBg] = ImVec4(0.13f, 0.17f, 0.23f, 1.00f);
            colors[ImGuiCol_TableBorderStrong] = ImVec4(0.23f, 0.28f, 0.36f, 1.00f);
            colors[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.22f, 0.29f, 1.00f);
            colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.53f, 0.87f, 0.45f);
            colors[ImGuiCol_DragDropTarget] = ImVec4(0.46f, 0.75f, 1.00f, 1.00f);
            colors[ImGuiCol_NavHighlight] = ImVec4(0.38f, 0.68f, 1.00f, 0.80f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
            colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.03f, 0.04f, 0.06f, 0.65f);
        }
    }

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
        // Persist layout alongside the running editor executable so startup layout
        // is stable regardless of current working directory.
        const std::filesystem::path layoutDirectory = ResolveImGuiLayoutDirectory();
        const std::filesystem::path activeLayoutPath = layoutDirectory / "imgui.ini";
        const std::filesystem::path defaultLayoutPath = ResolveDefaultLayoutPath(layoutDirectory);
        EnsureActiveLayoutExists(activeLayoutPath, defaultLayoutPath);
        m_LayoutIniPath = activeLayoutPath.string();
        m_DefaultLayoutIniPath = defaultLayoutPath.string();
        io.IniFilename = m_LayoutIniPath.c_str();

        ApplyModernEditorImGuiTheme();

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

    bool ImGuiLayer::SetLayoutIniPath(const std::filesystem::path& layoutIniPath)
    {
        if (!m_Initialized)
            return false;

        std::error_code errorCode;
        std::filesystem::create_directories(layoutIniPath.parent_path(), errorCode);
        if (errorCode)
            return false;

        m_LayoutIniPath = layoutIniPath.string();
        ImGui::GetIO().IniFilename = m_LayoutIniPath.c_str();
        return true;
    }

    bool ImGuiLayer::LoadLayoutFromDisk(const std::filesystem::path& layoutIniPath)
    {
        if (!m_Initialized)
            return false;

        std::error_code errorCode;
        if (!std::filesystem::exists(layoutIniPath, errorCode))
            return false;

        if (!SetLayoutIniPath(layoutIniPath))
            return false;

        m_DockSplitRatios.clear();
        m_LastDockspaceWorkWidth = 0.0f;
        m_LastDockspaceWorkHeight = 0.0f;
        m_PendingLayoutLoadPath = layoutIniPath.string();
        return true;
    }

    bool ImGuiLayer::SaveCurrentLayoutToDisk(const std::filesystem::path& layoutIniPath) const
    {
        if (!m_Initialized)
            return false;

        std::error_code errorCode;
        std::filesystem::create_directories(layoutIniPath.parent_path(), errorCode);
        if (errorCode)
            return false;

        ImGui::SaveIniSettingsToDisk(layoutIniPath.string().c_str());
        return true;
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

        m_SubmittedDockspaceThisFrame = false;
        bool loadedPendingLayoutThisFrame = false;
        if (!m_PendingLayoutLoadPath.empty())
        {
            const std::string pendingLayoutPath = m_PendingLayoutLoadPath;
            m_PendingLayoutLoadPath.clear();
            ImGui::ClearIniSettings();
            ImGui::LoadIniSettingsFromDisk(pendingLayoutPath.c_str());
            loadedPendingLayoutThisFrame = true;
        }

        if (m_ShowDockspace)
        {
            ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            const ImGuiID dockspaceId = GetMainViewportDockspaceId(mainViewport);
            if (mainViewport != nullptr)
            {
                ImGuiDockNode* dockspaceNode = ImGui::DockBuilderGetNode(dockspaceId);
                if (dockspaceNode != nullptr)
                {
                    if (loadedPendingLayoutThisFrame || m_DockSplitRatios.empty())
                        CaptureDockNodeSplitRatios(dockspaceNode, m_DockSplitRatios);

                    const ImVec2 currentDockspaceSize = dockspaceNode->Size;
                    const bool workSizeChanged = m_LastDockspaceWorkWidth > 0.0f &&
                                                 m_LastDockspaceWorkHeight > 0.0f &&
                                                 (std::fabs(m_LastDockspaceWorkWidth - mainViewport->WorkSize.x) > 0.5f ||
                                                  std::fabs(m_LastDockspaceWorkHeight - mainViewport->WorkSize.y) > 0.5f);
                    const bool dockspaceSizeDiffers = std::fabs(currentDockspaceSize.x - mainViewport->WorkSize.x) > 0.5f ||
                                                      std::fabs(currentDockspaceSize.y - mainViewport->WorkSize.y) > 0.5f;
                    const bool shouldProjectLayout = !m_DockSplitRatios.empty() &&
                                                     (loadedPendingLayoutThisFrame || workSizeChanged || dockspaceSizeDiffers);
                    if (shouldProjectLayout)
                        ProjectDockNodeSizeRefs(dockspaceNode, mainViewport->WorkSize, m_DockSplitRatios);
                }

                m_LastDockspaceWorkWidth = mainViewport->WorkSize.x;
                m_LastDockspaceWorkHeight = mainViewport->WorkSize.y;
            }

            ImGui::DockSpaceOverViewport(dockspaceId, mainViewport);
            m_SubmittedDockspaceThisFrame = true;
        }
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
        if (m_ShowDockspace && !m_SubmittedDockspaceThisFrame)
        {
            ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            ImGui::DockSpaceOverViewport(GetMainViewportDockspaceId(mainViewport), mainViewport);
        }

        m_SubmittedDockspaceThisFrame = false;

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

}  // namespace Limitless
