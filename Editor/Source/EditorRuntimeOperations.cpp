#include "EditorRuntimeOperations.h"

#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Renderer2D.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <glm/glm.hpp>

namespace Limitless::EditorRuntimeOperations
{
    namespace
    {
        enum class ViewportCaptureOwner : uint8_t
        {
            None = 0,
            Scene,
            Game
        };

        bool IsPointInsideRect(const ImVec2& point, const glm::vec2& rectMin, const glm::vec2& rectMax)
        {
            return point.x >= rectMin.x && point.x <= rectMax.x &&
                   point.y >= rectMin.y && point.y <= rectMax.y;
        }

        glm::vec2 ComputeRectCenter(const glm::vec2& rectMin, const glm::vec2& rectMax)
        {
            return glm::vec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
        }
    }

    void Attach(uint32_t viewportWidthPixels,
                uint32_t viewportHeightPixels,
                std::unique_ptr<Scene>& scene,
                CameraManager& cameraManager,
                CameraId& editorCameraId,
                std::unique_ptr<EditorCameraController>& editorCameraController,
                std::shared_ptr<Framebuffer>& viewportFramebuffer)
    {
        Renderer2D::Initialize();

        scene = std::make_unique<Scene>();

        CameraManager::Perspective3DCreateInfo cameraInfo{};
        cameraInfo.Name = "EditorCamera";
        cameraInfo.Usage = CameraUsage::Editor;
        cameraInfo.ViewportWidthPixels = viewportWidthPixels;
        cameraInfo.ViewportHeightPixels = viewportHeightPixels;
        cameraInfo.FieldOfViewYDegrees = 60.0f;
        cameraInfo.NearPlane = 0.1f;
        cameraInfo.FarPlane = 1000.0f;

        editorCameraId = cameraManager.CreatePerspective3D(cameraInfo);
        cameraManager.SetActiveCamera(editorCameraId);

        if (auto* camera = cameraManager.GetPerspective3D(editorCameraId))
        {
            camera->SetPosition(glm::vec3(0.0f, 0.0f, 2.0f));
            camera->SetYawPitchDegrees(-90.0f, 0.0f);
        }

        editorCameraController = std::make_unique<EditorCameraController>();
        EditorCameraController::Settings editorCameraSettings{};
        editorCameraSettings.InputActionsAssetKey = "Assets/InputActions/EditorCamera.inputactions.json";
        editorCameraSettings.UseOverrideActionAsset = true;
        editorCameraController->Initialize(cameraManager, editorCameraId, editorCameraSettings);

        EnsureViewportFramebuffer(
            viewportWidthPixels,
            viewportHeightPixels,
            viewportFramebuffer,
            viewportWidthPixels,
            viewportHeightPixels,
            editorCameraController.get());

        LT_INFO("EditorLayer attached");
    }

    void Detach(std::unique_ptr<Scene>& scene,
                std::unique_ptr<Scene>& editSceneStored,
                std::unique_ptr<EditorCameraController>& editorCameraController,
                std::shared_ptr<Framebuffer>& viewportFramebuffer)
    {
        if (editorCameraController)
        {
            editorCameraController->Shutdown();
            editorCameraController.reset();
        }

        scene.reset();
        editSceneStored.reset();
        viewportFramebuffer.reset();
        Renderer2D::Shutdown();

        LT_INFO("EditorLayer detached");
    }

    void Update(EditorPlayModeState playModeState,
                bool sceneViewHovered,
                bool sceneViewRectValid,
                const glm::vec2& sceneViewRectMinPixels,
                const glm::vec2& sceneViewRectMaxPixels,
                bool gameViewRectValid,
                const glm::vec2& gameViewRectMinPixels,
                const glm::vec2& gameViewRectMaxPixels,
                bool textInputWanted,
                float deltaTime,
                EditorCameraController* editorCameraController)
    {
        if (!editorCameraController)
            return;

        static ViewportCaptureOwner s_CaptureOwner = ViewportCaptureOwner::None;
        static bool s_RightMouseWasDown = false;

        (void)playModeState;
        const bool rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool rightMousePressedThisFrame = rightMouseDown && !s_RightMouseWasDown;
        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool mouseInSceneView = sceneViewRectValid && IsPointInsideRect(mousePosition, sceneViewRectMinPixels, sceneViewRectMaxPixels);
        const bool mouseInGameView = gameViewRectValid && IsPointInsideRect(mousePosition, gameViewRectMinPixels, gameViewRectMaxPixels);

        if (textInputWanted)
        {
            s_CaptureOwner = ViewportCaptureOwner::None;
        }
        else
        {
            if (rightMousePressedThisFrame)
            {
                if (mouseInSceneView)
                    s_CaptureOwner = ViewportCaptureOwner::Scene;
                else if (mouseInGameView)
                    s_CaptureOwner = ViewportCaptureOwner::Game;
                else
                    s_CaptureOwner = ViewportCaptureOwner::None;
            }
            else if (!rightMouseDown)
            {
                s_CaptureOwner = ViewportCaptureOwner::None;
            }
        }

        const bool sceneLookCaptureActive = !textInputWanted && rightMouseDown && s_CaptureOwner == ViewportCaptureOwner::Scene;
        const bool gameLookCaptureActive = !textInputWanted && rightMouseDown && s_CaptureOwner == ViewportCaptureOwner::Game;
        const bool allowCameraInput = sceneLookCaptureActive || (!textInputWanted && sceneViewHovered && !rightMouseDown);

        editorCameraController->SetInputEnabled(allowCameraInput);
        editorCameraController->Update(deltaTime);

        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            const bool shouldLockCursor = sceneLookCaptureActive || gameLookCaptureActive;

            if (window.IsCursorLocked() != shouldLockCursor)
                window.SetCursorLocked(shouldLockCursor);
            window.SetCursorVisible(!shouldLockCursor);

            if (shouldLockCursor)
            {
                const bool useSceneCenter = (s_CaptureOwner == ViewportCaptureOwner::Scene);
                const glm::vec2 center = useSceneCenter
                    ? ComputeRectCenter(sceneViewRectMinPixels, sceneViewRectMaxPixels)
                    : ComputeRectCenter(gameViewRectMinPixels, gameViewRectMaxPixels);
                window.SetCursorPosition(static_cast<int>(center.x), static_cast<int>(center.y));
            }
        }

        s_RightMouseWasDown = rightMouseDown;
    }

    void HandleWindowResize(uint32_t width,
                            uint32_t height,
                            uint32_t& viewportWidthPixels,
                            uint32_t& viewportHeightPixels,
                            std::shared_ptr<Framebuffer>& viewportFramebuffer,
                            EditorCameraController* editorCameraController)
    {
        if (width == 0 || height == 0)
            return;

        viewportWidthPixels = width;
        viewportHeightPixels = height;

        EnsureViewportFramebuffer(
            width,
            height,
            viewportFramebuffer,
            viewportWidthPixels,
            viewportHeightPixels,
            editorCameraController);
    }

    void EnsureViewportFramebuffer(uint32_t width,
                                   uint32_t height,
                                   std::shared_ptr<Framebuffer>& viewportFramebuffer,
                                   uint32_t& viewportWidthPixels,
                                   uint32_t& viewportHeightPixels,
                                   EditorCameraController* editorCameraController)
    {
        if (width == 0 || height == 0)
            return;

        constexpr uint32_t minimumViewportSize = 32;
        width = (width < minimumViewportSize) ? minimumViewportSize : width;
        height = (height < minimumViewportSize) ? minimumViewportSize : height;

        if (!viewportFramebuffer || viewportFramebuffer->GetWidth() != width || viewportFramebuffer->GetHeight() != height)
        {
            FramebufferSpecification specification{};
            specification.Width = width;
            specification.Height = height;
            specification.Samples = 1;
            specification.DepthAttachment = true;
            specification.StencilAttachment = false;

            viewportFramebuffer = Framebuffer::Create(specification);
            viewportWidthPixels = width;
            viewportHeightPixels = height;
            if (editorCameraController)
                editorCameraController->OnWindowResize(width, height);
        }
    }
}
