#include "TestLayer.h"

#include "EditorCameraController.h"
#include "TexturedTriangleDemo.h"

namespace Limitless
{
    TestLayer::TestLayer()
        : Layer("TestLayer")
    {
        LT_INFO("TestLayer created");
    }

    TestLayer::~TestLayer() = default;

    void TestLayer::OnAttach()
    {
        LT_INFO("TestLayer attached");

        // Load project-wide input actions from a Unity-style asset (JSON in Assets/).
        // EditorCameraController will still push its own override (Unity/editor style), but this validates the
        // project-wide asset path and ensures gameplay code can rely on it.
        GetInputSystem().SetProjectActionAssetFromKey("Assets/InputActions/Sandbox.inputactions.json");

        // Create an editor camera (3D) and make it active.
        CameraManager::Perspective3DCreateInfo cameraInfo{};
        cameraInfo.Name = "EditorCamera";
        cameraInfo.Usage = CameraUsage::Editor;
        cameraInfo.ViewportWidthPixels = 1280;
        cameraInfo.ViewportHeightPixels = 720;
        cameraInfo.FieldOfViewYDegrees = 60.0f;
        cameraInfo.NearPlane = 0.1f;
        cameraInfo.FarPlane = 1000.0f;

        m_CameraId = m_CameraManager.CreatePerspective3D(cameraInfo);
        m_CameraManager.SetActiveCamera(m_CameraId);

        if (auto* camera = m_CameraManager.GetPerspective3D(m_CameraId))
        {
            camera->SetPosition(glm::vec3(0.0f, 0.0f, 2.0f));
            camera->SetYawPitchDegrees(-90.0f, 0.0f);
        }

        m_TriangleDemo = std::make_unique<TexturedTriangleDemo>();
        m_TriangleDemo->Initialize();

        m_EditorCameraController = std::make_unique<EditorCameraController>();
        m_EditorCameraController->Initialize(m_CameraManager, m_CameraId);
    }

    void TestLayer::OnDetach()
    {
        LT_INFO("TestLayer detached");

        if (m_EditorCameraController)
        {
            m_EditorCameraController->Shutdown();
            m_EditorCameraController.reset();
        }

        if (m_TriangleDemo)
        {
            m_TriangleDemo->Shutdown();
            m_TriangleDemo.reset();
        }
    }

    void TestLayer::OnUpdate(float deltaTime)
    {
        if (m_TriangleDemo)
        {
            m_TriangleDemo->Update(deltaTime);
        }

        if (m_EditorCameraController)
        {
            m_EditorCameraController->Update(deltaTime);
        }
    }

    void TestLayer::OnRender()
    {
        if (m_TriangleDemo)
        {
            m_TriangleDemo->Render(m_CameraManager);
        }
    }

    void TestLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        if (m_EditorCameraController)
        {
            m_EditorCameraController->OnWindowResize(event.GetWidth(), event.GetHeight());
        }
        else if (auto* camera = m_CameraManager.GetCamera(m_CameraId))
        {
            camera->SetViewportSize(event.GetWidth(), event.GetHeight());
        }
    }
} 