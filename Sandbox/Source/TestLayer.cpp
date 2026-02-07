#include "TestLayer.h"

#include "Editor/EditorCameraController.h"
#include "TexturedTriangleDemo.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetBundleBuilder.h"
#include "Assets/AssetHotReloadManager.h"

#include "Platform/Platform.h"

#include <SDL3/SDL_keyboard.h>

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

        // Engine startup may have auto-enabled the AssetBundle already.
        // If not, we keep dev hot reload enabled for source-asset workflows.
        if (Assets::AssetBundle::GetInstance().IsEnabled())
        {
            m_UsingAssetBundle = true;
        }
        else
        {
            Assets::AssetHotReloadManager::GetInstance().Enable(true);
        }

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
        EditorCameraController::Settings editorCameraSettings{};
        editorCameraSettings.InputActionsAssetKey = "Assets/InputActions/EditorCamera.inputactions.json";
        editorCameraSettings.UseOverrideActionAsset = true;
        m_EditorCameraController->Initialize(m_CameraManager, m_CameraId, editorCameraSettings);
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
        // Press B to build a runtime AssetBundle and toggle loading from it.
        // This simulates a "shipped build" where the source `Assets/` folder is not present.
        if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_B))
        {
            if (!m_UsingAssetBundle)
            {
                const std::filesystem::path exeDir = std::filesystem::path(PlatformDetection::GetExecutablePath()).parent_path();
                const std::filesystem::path outDir = exeDir / "AssetBundle";
                const auto built = Assets::AssetBundleBuilder::BuildAssetBundleToDirectory(outDir);
                if (built.IsFailure())
                {
                    LT_CORE_ERROR("AssetBundle build failed: {}", built.GetError().GetErrorMessage());
                }
                else
                {
                    auto& bundle = Assets::AssetBundle::GetInstance();
                    const auto loaded = bundle.LoadFromDirectory(outDir);
                    if (loaded.IsFailure())
                    {
                        LT_CORE_ERROR("AssetBundle load failed: {}", loaded.GetError().GetErrorMessage());
                    }
                    else
                    {
                        bundle.Enable(true);
                        m_UsingAssetBundle = true;
                        Assets::AssetHotReloadManager::GetInstance().Enable(false);
                        LT_INFO("AssetBundle enabled (loading assets from bundle).");
                    }
                }
            }
            else
            {
                auto& bundle = Assets::AssetBundle::GetInstance();
                bundle.Enable(false);
                m_UsingAssetBundle = false;
                Assets::AssetHotReloadManager::GetInstance().Enable(true);
                LT_INFO("AssetBundle disabled (loading assets from source Assets/).");
            }

            // Reload demo and input assets so we prove the new source works.
            GetInputSystem().SetProjectActionAssetFromKey("Assets/InputActions/Sandbox.inputactions.json");

            if (m_TriangleDemo)
            {
                m_TriangleDemo->Shutdown();
                m_TriangleDemo->Initialize();
            }

            if (m_EditorCameraController)
            {
                m_EditorCameraController->Shutdown();
                m_EditorCameraController.reset();

                m_EditorCameraController = std::make_unique<EditorCameraController>();
                EditorCameraController::Settings editorCameraSettings{};
                editorCameraSettings.InputActionsAssetKey = "Assets/InputActions/EditorCamera.inputactions.json";
                editorCameraSettings.UseOverrideActionAsset = true;
                m_EditorCameraController->Initialize(m_CameraManager, m_CameraId, editorCameraSettings);
            }
        }

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