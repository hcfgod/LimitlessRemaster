#pragma once

#include "ImGuiBackend.h"

namespace Limitless
{
    class OpenGLContext;

    /// OpenGL implementation of the ImGui renderer backend.
    ///
    /// Wraps imgui_impl_opengl3 and manages OpenGLContext::ScopedCurrentContext
    /// acquisition so that ImGuiLayer never touches GL types directly.
    class ImGuiOpenGLBackend final : public ImGuiBackend
    {
    public:
        ImGuiOpenGLBackend() = default;
        ~ImGuiOpenGLBackend() override = default;

        bool Init(Window& window, GraphicsContext* context) override;
        void Shutdown() override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData) override;

    private:
        OpenGLContext* m_GLContext = nullptr; // borrowed, not owned
    };

}  // namespace Limitless
