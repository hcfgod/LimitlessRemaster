#include "EditorCameraController.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"

namespace Limitless
{
    void EditorCameraController::Initialize(CameraManager& cameraManager, CameraId cameraId)
    {
        m_CameraManager = &cameraManager;
        m_CameraId = cameraId;
        EnsureInputAsset();

        // Non-project-wide override: editor camera input should not overwrite the project default.
        GetInputSystem().PushOverrideActionAsset(m_InputAsset);
    }

    void EditorCameraController::Shutdown()
    {
        if (m_InputAsset)
        {
            GetInputSystem().PopOverrideActionAsset(m_InputAsset);
        }

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
    }

    void EditorCameraController::Update(float deltaTime)
    {
        if (!m_CameraManager || !m_CameraId)
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

        // Prefer Unity-style asset file so Sandbox can validate the full pipeline.
        m_InputAssetResource = Assets::AssetManager::LoadBlocking<Assets::InputActionsAssetResource>("Assets/InputActions/Sandbox.inputactions.json");
        if (m_InputAssetResource && m_InputAssetResource->GetValue())
        {
            m_InputAsset = m_InputAssetResource->GetValue();
        }
        else
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
            LT_CORE_ERROR("EditorCameraController: InputActionAsset missing map 'Editor'");
            return;
        }

        m_ActionMove = map->FindAction("Move");
        m_ActionLook = map->FindAction("Look");
        m_ActionBoost = map->FindAction("Boost");
        m_ActionLookEnable = map->FindAction("LookEnable");
    }

    void EditorCameraController::RefreshInputAssetIfHotReloaded()
    {
        if (!m_InputAssetResource)
        {
            return;
        }

        auto latest = m_InputAssetResource->GetValue();
        if (!latest || latest == m_InputAsset)
        {
            return;
        }

        // Swap override asset reference.
        if (m_InputAsset)
        {
            GetInputSystem().PopOverrideActionAsset(m_InputAsset);
        }

        m_InputAsset = latest;
        GetInputSystem().PushOverrideActionAsset(m_InputAsset);

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

