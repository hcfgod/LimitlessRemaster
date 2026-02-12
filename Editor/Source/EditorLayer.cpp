#include "EditorLayer.h"
#include "Assets/TextureAsset.h"
#include "Editor/EditorCameraController.h"
#include "EditorInspectorPanel.h"
#include "EditorMenuBar.h"
#include "EditorPlayMode.h"
#include "EditorProjectPanel.h"
#include "EditorRuntimeOperations.h"
#include "EditorScenePanel.h"
#include "EditorViewportPanel.h"
#include "ImGui/ImGuiLayer.h"
#include "imgui/imgui.h"

namespace Limitless
{
    namespace
    {
        constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";

    }  // anonymous namespace

    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        EditorRuntimeOperations::Attach(
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_Scene,
            m_CameraManager,
            m_EditorCameraId,
            m_EditorCameraController,
            m_ViewportFramebuffer);
    }

    void EditorLayer::OnDetach()
    {
        EditorRuntimeOperations::Detach(
            m_Scene,
            m_EditSceneStored,
            m_EditorCameraController,
            m_ViewportFramebuffer);
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        const ImGuiIO& io = ImGui::GetIO();
        EditorRuntimeOperations::Update(
            m_PlayModeState,
            m_ViewportHovered,
            io.WantTextInput,
            deltaTime,
            m_EditorCameraController.get());
    }

    void EditorLayer::OnRender()
    {
        DrawMenuBar();
        DrawViewportPanel();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawProjectPanel();

        if (m_ShowDemoWindow)
            ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    void EditorLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        EditorRuntimeOperations::HandleWindowResize(
            event.GetWidth(),
            event.GetHeight(),
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
            m_EditorCameraController.get());
    }

    void EditorLayer::DrawMenuBar()
    {
        EditorMenuBar::Draw(
            m_PlayModeState,
            m_ShowDemoWindow,
            [this]() { EnterPlayMode(); },
            [this]() { ExitPlayMode(); },
            [this]() { TogglePausePlayMode(); });
    }

    void EditorLayer::DrawViewportPanel()
    {
        EditorViewportPanel::Draw(
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_ViewportFramebuffer,
            m_ViewportFocused,
            m_ViewportHovered,
            m_EditorCameraController.get(),
            m_CameraManager,
            m_Scene.get(),
            m_PlayModeState,
            m_PlayModeMissingGameplayCamera,
            [this](uint32_t width, uint32_t height) { EnsureViewportFramebuffer(width, height); });
    }

    void EditorLayer::DrawScenePanel()
    {
        EditorScenePanel::Draw(
            m_Scene.get(),
            m_ScenePanelState,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::DrawInspectorPanel()
    {
        EditorInspectorPanel::Draw(
            m_Scene.get(),
            m_SelectedEntity,
            kAssetTexturePayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::DrawProjectPanel()
    {
        EditorProjectPanel::Draw(
            m_ProjectPanelState,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            kAssetTexturePayload,
            kAssetMovePayload);
    }

    void EditorLayer::EnsureViewportFramebuffer(uint32_t width, uint32_t height)
    {
        EditorRuntimeOperations::EnsureViewportFramebuffer(
            width,
            height,
            m_ViewportFramebuffer,
            m_ViewportWidthPixels,
            m_ViewportHeightPixels,
            m_EditorCameraController.get());
    }

    void EditorLayer::EnterPlayMode()
    {
        EditorPlayMode::Enter(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_CachedGameplayCameraId,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::ExitPlayMode()
    {
        EditorPlayMode::Exit(
            m_PlayModeState,
            m_Scene,
            m_EditSceneStored,
            m_CameraManager,
            m_EditorCameraId,
            m_CachedGameplayCameraId,
            m_PlayModeMissingGameplayCamera,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset);
    }

    void EditorLayer::TogglePausePlayMode()
    {
        EditorPlayMode::TogglePause(m_PlayModeState);
    }

}  // namespace Limitless
