#include "TestLayer.h"

#include "Editor/EditorCameraController.h"
#include "Renderer2DDemo.h"
#include "AudioDemo.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetBundleBuilder.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetManager.h"
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
        GetInputSystem().SetProjectActionAssetFromKey("Assets/InputActions/Runtime.inputactions.json");

        // Create an editor camera (3D) and make it active.
        CameraManager::Perspective3DCreateInfo cameraInfo{};
        cameraInfo.Name = "EditorCamera";
        cameraInfo.Usage = CameraUsage::Editor;
        cameraInfo.ViewportWidthPixels = m_ViewportWidthPixels;
        cameraInfo.ViewportHeightPixels = m_ViewportHeightPixels;
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

        m_Renderer2DDemo = std::make_unique<Renderer2DDemo>();
        m_Renderer2DDemo->Initialize(m_ViewportWidthPixels, m_ViewportHeightPixels);

        m_AudioDemo = std::make_unique<AudioDemo>();
        m_AudioDemo->Initialize();

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

        if (m_Renderer2DDemo)
        {
            m_Renderer2DDemo->Shutdown();
            m_Renderer2DDemo.reset();
        }

        if (m_AudioDemo)
        {
            m_AudioDemo->Shutdown();
            m_AudioDemo.reset();
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

            // Cancel in-flight loads and clear caches so subsequent loads come from the new source.
            Assets::AssetLoadCoordinator::CancelAllInFlightLoads();
            Assets::AssetManager::ClearCaches();

            // Reload demo and input assets so we prove the new source works.
            GetInputSystem().SetProjectActionAssetFromKey("Assets/InputActions/Runtime.inputactions.json");

            if (m_Renderer2DDemo)
            {
                m_Renderer2DDemo->Shutdown();
                m_Renderer2DDemo->Initialize(m_ViewportWidthPixels, m_ViewportHeightPixels);
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

        // Renderer2D performance stress test controls (demo-only).
        // - 1/2/3/4: apply quad-count presets
        // - T: toggle texture alternation (best-case batching vs state-change stress)
        // - G: toggle stress test grid on/off (fallback minimal scene)
        if (m_Renderer2DDemo)
        {
            if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_1))
            {
                m_Renderer2DDemo->ApplyStressPreset(1);
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_2))
            {
                m_Renderer2DDemo->ApplyStressPreset(2);
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_3))
            {
                m_Renderer2DDemo->ApplyStressPreset(3);
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_4))
            {
                m_Renderer2DDemo->ApplyStressPreset(4);
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_T))
            {
                auto settings = m_Renderer2DDemo->GetStressTestSettings();
                settings.AlternateTextures = !settings.AlternateTextures;
                m_Renderer2DDemo->SetStressTestSettings(settings);
                LT_INFO("Renderer2DDemo: AlternateTextures={}", settings.AlternateTextures);
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_G))
            {
                auto settings = m_Renderer2DDemo->GetStressTestSettings();
                settings.Enabled = !settings.Enabled;
                m_Renderer2DDemo->SetStressTestSettings(settings);
                LT_INFO("Renderer2DDemo: Stress grid Enabled={}", settings.Enabled);
            }
        }

        // Audio demo controls:
        // - P: play clip (Assets/Audio/Example.wav by default)
        // - O: stop clip
        if (m_AudioDemo)
        {
            if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_P))
            {
                m_AudioDemo->Play();
            }
            else if (GetInputSystem().WasKeyPressedThisFrame(SDL_SCANCODE_O))
            {
                m_AudioDemo->Stop();
            }
        }

        if (m_Renderer2DDemo)
        {
            m_Renderer2DDemo->Update(deltaTime);
        }

        if (m_EditorCameraController)
        {
            m_EditorCameraController->Update(deltaTime);
        }
    }

    void TestLayer::OnRender()
    {
        if (m_Renderer2DDemo)
        {
            if (const auto* camera = m_CameraManager.GetCamera(m_CameraId))
            {
                m_Renderer2DDemo->Render(*camera);
            }
        }
    }

    void TestLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        m_ViewportWidthPixels = event.GetWidth();
        m_ViewportHeightPixels = event.GetHeight();

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