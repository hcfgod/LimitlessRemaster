#include "EditorApp.h"
#include "EditorLayer.h"
#include "ImGui/ImGuiLayer.h"
#include "Core/Debug/Log.h"

// CreateApplication is defined here; main() comes from Limitless.h via LT_ENABLE_ENTRYPOINT

namespace Limitless
{
    bool EditorApp::Initialize()
    {
        LT_INFO("EditorApp Initialize");

        // ImGui layer first (dockspace) - must run before panels per ImGui docs.
        PushLayer(CreateLayer<ImGuiLayer>());

        // Editor layer (viewport, scene, inspector, project panels).
        PushLayer(CreateLayer<EditorLayer>());

        LT_INFO("Editor layers pushed to layer stack");

        return true;
    }

    void EditorApp::Shutdown()
    {
        LT_INFO("EditorApp Shutdown");
    }
}

std::unique_ptr<Limitless::Application> CreateApplication()
{
    return std::make_unique<Limitless::EditorApp>();
}