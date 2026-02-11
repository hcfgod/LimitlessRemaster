#include "Editor/EditorCameraController.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"

namespace Limitless
{
    void EditorCameraController::Initialize(CameraManager& cameraManager, CameraId cameraId, Settings settings)
    {
        m_Settings = std::move(settings);
        m_CameraManager = &cameraManager;
        m_CameraId = cameraId;
        EnsureInputAsset();

        // Non-project-wide override: editor camera input should not overwrite the project default.
        if (m_Settings.UseOverrideActionAsset && m_InputAsset)
        {
            GetInputSystem().PushOverrideActionAsset(m_InputAsset);
        }
    }

    void EditorCameraController::Shutdown()
    {
        if (m_InputEnabled && m_Settings.UseOverrideActionAsset && m_InputAsset)
        {
            GetInputSystem().PopOverrideActionAsset(m_InputAsset);
        }
        m_InputEnabled = false;

        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            window.SetCursorLocked(false);
            window.SetCursorVisible(true);
        }

        m_CameraManager = nullptr;
        m_CameraId = {};
        m_ActionMove = nullptr;
        m_ActionLook = nullptr;
        m_ActionBoost = nullptr;
        m_ActionLookEnable = nullptr;
        m_InputAsset.reset();
        m_InputAssetResource.reset();
        m_InputAssetRevision = 0;
        m_Settings = Settings{};
    }

    void EditorCameraController::SetInputEnabled(bool enabled)
    {
        if (m_InputEnabled == enabled)
            return;

        m_InputEnabled = enabled;

        if (!enabled)
        {
            if (m_Settings.UseOverrideActionAsset && m_InputAsset)
            {
                GetInputSystem().PopOverrideActionAsset(m_InputAsset);
            }
            if (Application::HasInstance())
            {
                auto& window = Application::GetInstance().GetWindow();
                window.SetCursorLocked(false);
                window.SetCursorVisible(true);
            }
        }
        else
        {
            if (m_Settings.UseOverrideActionAsset && m_InputAsset)
            {
                GetInputSystem().PushOverrideActionAsset(m_InputAsset);
            }
        }
    }

    void EditorCameraController::Update(float deltaTime)
    {
        if (!m_CameraManager || !m_CameraId || !m_InputEnabled)
        {
            return;
        }

        RefreshInputAssetIfHotReloaded();

        auto* camera = m_CameraManager->GetPerspective3D(m_CameraId);
        if (!camera || !m_ActionMove || !m_ActionLook)
        {
            return;
        }

        const bool wantLook = (m_ActionLookEnable != nullptr) ? m_ActionLookEnable->ReadButton() : true;

        // Unity/editor style: lock+hide cursor while RMB is held.
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            window.SetCursorLocked(wantLook);
            window.SetCursorVisible(!wantLook);
        }

        const glm::vec2 move = m_ActionMove->ReadAxis2D();
        const glm::vec2 look = wantLook ? m_ActionLook->ReadAxis2D() : glm::vec2(0.0f);

        // Mouse delta -> yaw/pitch (scaled; camera stores degrees).
        const float yaw = camera->GetYawDegrees() + (look.x * (LookSensitivity * 180.0f / 3.14159265f));
        const float pitch = camera->GetPitchDegrees() + (-look.y * (LookSensitivity * 180.0f / 3.14159265f));
        camera->SetYawPitchDegrees(yaw, pitch);

        const float boost = (m_ActionBoost && m_ActionBoost->ReadButton()) ? BoostMultiplier : 1.0f;
        const float speed = MoveSpeed * boost;

        const glm::vec3 forward = camera->GetForwardDirection();
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 position = camera->GetPosition();
        position += forward * (move.y * speed * deltaTime);
        position += right * (move.x * speed * deltaTime);
        camera->SetPosition(position);
    }

    void EditorCameraController::OnWindowResize(uint32_t widthPixels, uint32_t heightPixels)
    {
        if (m_CameraManager)
        {
            if (auto* camera = m_CameraManager->GetCamera(m_CameraId))
            {
                camera->SetViewportSize(widthPixels, heightPixels);
            }
        }
    }

    void EditorCameraController::EnsureInputAsset()
    {
        if (m_InputAsset)
        {
            return;
        }

        // Prefer Unity-style asset file so editor tools validate the full pipeline.
        if (!m_Settings.InputActionsAssetKey.empty())
        {
            m_InputAssetResource = Assets::AssetManager::LoadBlocking<Assets::InputActionsAssetResource>(m_Settings.InputActionsAssetKey);
            if (m_InputAssetResource && m_InputAssetResource->GetValue())
            {
                m_InputAsset = m_InputAssetResource->GetValue();
            }
        }

        if (!m_InputAsset)
        {
            // Fallback to hardcoded bindings (keeps controller usable even without assets).
            m_InputAsset = std::make_shared<InputActionAsset>();
            auto& map = m_InputAsset->AddMap("Editor");

            auto& move = map.AddAction("Move", InputActionValueType::Axis2D);
            move.AddBinding(KeyboardAxis2DBinding{
                .Up = SDL_SCANCODE_W,
                .Down = SDL_SCANCODE_S,
                .Left = SDL_SCANCODE_A,
                .Right = SDL_SCANCODE_D,
                .Scale = 1.0f
            });

            auto& look = map.AddAction("Look", InputActionValueType::Axis2D);
            look.AddBinding(MouseDeltaBinding{
                .Sensitivity = 1.0f,
                .InvertY = false
            });

            auto& boost = map.AddAction("Boost", InputActionValueType::Button);
            boost.AddBinding(KeyboardButtonBinding{ .Key = SDL_SCANCODE_LSHIFT });

            auto& lookEnable = map.AddAction("LookEnable", InputActionValueType::Button);
            lookEnable.AddBinding(MouseButtonBinding{ .Button = SDL_BUTTON_RIGHT });
        }

        // Bind action pointers from the loaded asset.
        auto* map = m_InputAsset ? m_InputAsset->FindMap("Editor") : nullptr;
        if (!map)
        {
            LT_CORE_ERROR("EditorCameraController: InputActionAsset missing map 'Editor' (assetKey='{}')", m_Settings.InputActionsAssetKey);
            return;
        }

        m_ActionMove = map->FindAction("Move");
        m_ActionLook = map->FindAction("Look");
        m_ActionBoost = map->FindAction("Boost");
        m_ActionLookEnable = map->FindAction("LookEnable");

        // Seed revision tracking for hot reload.
        m_InputAssetRevision = m_InputAssetResource ? m_InputAssetResource->GetRevision() : 0;
    }

    void EditorCameraController::RefreshInputAssetIfHotReloaded()
    {
        if (!m_InputAssetResource)
        {
            return;
        }

        const uint64_t latestRevision = m_InputAssetResource->GetRevision();
        if (latestRevision == m_InputAssetRevision)
        {
            return;
        }

        // Pointer is stable (reloaded in-place). We only need to refresh cached action pointers.
        m_InputAssetRevision = latestRevision;

        auto* map = m_InputAsset->FindMap("Editor");
        if (!map)
        {
            LT_CORE_ERROR("EditorCameraController: hot reloaded InputActionAsset missing map 'Editor'");
            return;
        }

        m_ActionMove = map->FindAction("Move");
        m_ActionLook = map->FindAction("Look");
        m_ActionBoost = map->FindAction("Boost");
        m_ActionLookEnable = map->FindAction("LookEnable");

        LT_INFO("EditorCameraController: input actions hot reloaded from asset");
    }
}

