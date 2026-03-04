#pragma once

#include "Core/Layer.h"
#include "Graphics/GraphicsContext.h"

#include <string>

namespace Limitless
{
    /// Layer that integrates Dear ImGui with SDL3 and multiple graphics backends.
    ///
    /// Supports OpenGL3 by default; Vulkan, DirectX, and Metal backends are
    /// prepared for future implementation. Uses context->GetAPI() to select
    /// the appropriate renderer. Push as an overlay so it renders on top of all
    /// other layers. Registers ImGui lifecycle callbacks with Application for
    /// correct frame ordering. Docking is enabled by default.
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnRender() override;

        /// Called by Application before layer updates (begin ImGui frame).
        void BeginFrame();
        /// Called by Application after layer render (render ImGui draw data).
        void EndFrame();

    private:
        void OnAttachOpenGL(class Window& window, class GraphicsContext* context);

        bool m_Initialized = false;
        GraphicsAPI m_GraphicsAPI = GraphicsAPI::OpenGL;  ///< Active backend (set at attach).
        bool m_ShowDemoWindow = false;  ///< Toggle ImGui demo window (EditorLayer provides its own via menu).
        bool m_ShowDockspace = true;  ///< Toggle default dockspace (enabled by default).
        std::string m_LayoutIniPath;  ///< Stable storage for ImGuiIO::IniFilename.
    };

}  // namespace Limitless
