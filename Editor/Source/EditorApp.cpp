#include "EditorApp.h"
#include "ImGui/ImGuiLayer.h"
#include "Core/Debug/Log.h"
#include "imgui/imgui.h"

// CreateApplication is defined here; main() comes from Limitless.h via LT_ENABLE_ENTRYPOINT

namespace Limitless
{
    bool EditorApp::Initialize()
    {
        LT_INFO("EditorApp Initialize");

        // Push ImGui overlay first so it renders on top of all layers.
        PushOverlay(CreateLayer<ImGuiLayer>());

        LT_INFO("ImGuiLayer pushed to layer stack");

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
