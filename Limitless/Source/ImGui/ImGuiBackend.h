#pragma once

#include <memory>

struct ImDrawData;

namespace Limitless
{
    class Window;
    class GraphicsContext;
    enum class GraphicsAPI;

    /// Backend-neutral interface for ImGui rendering integration.
    ///
    /// Each graphics API provides a concrete implementation that handles
    /// platform/renderer init, per-frame setup, and draw-data submission.
    /// ImGuiLayer holds an ImGuiBackend* and delegates all API-specific
    /// work through this interface.
    class ImGuiBackend
    {
    public:
        virtual ~ImGuiBackend() = default;

        /// Initialise the ImGui renderer backend.
        /// Called once during ImGuiLayer::OnAttach after the ImGui context and
        /// SDL3 platform backend have been created.
        /// @return true on success.
        virtual bool Init(Window& window, GraphicsContext* context) = 0;

        /// Tear down the ImGui renderer backend.
        /// Called during ImGuiLayer::OnDetach before the ImGui context is destroyed.
        virtual void Shutdown() = 0;

        /// Begin a new ImGui renderer frame (e.g. ImGui_ImplOpenGL3_NewFrame).
        /// Called at the start of each frame before ImGui::NewFrame().
        virtual void NewFrame() = 0;

        /// Submit the ImGui draw data to the GPU.
        /// Called after ImGui::Render() with the result of ImGui::GetDrawData().
        /// Implementations should acquire the graphics context if needed,
        /// prepare the default framebuffer, and issue the draw calls.
        virtual void RenderDrawData(ImDrawData* drawData) = 0;

        // ---- Factory --------------------------------------------------------

        /// Create the appropriate ImGui backend for the given graphics API.
        static std::unique_ptr<ImGuiBackend> Create(GraphicsAPI api);
    };

}  // namespace Limitless
