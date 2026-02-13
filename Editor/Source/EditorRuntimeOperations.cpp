#include "EditorRuntimeOperations.h"

#include "Core/Debug/Log.h"
#include "Editor/EditorCameraController.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Renderer2D.h"
#include "Scene/Scene.h"

#include <glm/glm.hpp>

namespace Limitless::EditorRuntimeOperations
{
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
                bool viewportHovered,
                bool textInputWanted,
                float deltaTime,
                EditorCameraController* editorCameraController)
    {
        if (!editorCameraController)
            return;

        const bool allowCameraInput =
            (playModeState == EditorPlayModeState::Edit) &&
            viewportHovered &&
            !textInputWanted;

        editorCameraController->SetInputEnabled(allowCameraInput);
        editorCameraController->Update(deltaTime);
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
