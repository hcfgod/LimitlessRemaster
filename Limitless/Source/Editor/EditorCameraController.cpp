#include "Editor/EditorCameraController.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <glm/geometric.hpp>

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
        m_WasLookActive = false;
        m_WasBlenderNavigationActive = false;
        m_OrbitTarget = glm::vec3(0.0f);
        m_OrbitDistance = 5.0f;
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
            m_WasLookActive = false;
            m_WasBlenderNavigationActive = false;
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

        const ImGuiIO& io = ImGui::GetIO();
        const bool shiftDown = io.KeyShift;
        const bool ctrlDown = io.KeyCtrl;
        const bool middleMouseDown = io.MouseDown[ImGuiMouseButton_Middle];
        const bool rightMouseDown = io.MouseDown[ImGuiMouseButton_Right];

        // -- Blender-style navigation detection --
        // MMB = orbit, Shift+MMB = pan, Ctrl+MMB = dolly drag, Wheel = dolly step
        const bool blenderOrbitActive = !rightMouseDown && middleMouseDown && !shiftDown && !ctrlDown;
        const bool blenderPanActive = !rightMouseDown && middleMouseDown && shiftDown && !ctrlDown;
        const bool blenderDollyDragActive = !rightMouseDown && middleMouseDown && ctrlDown;
        const bool blenderWheelActive = !rightMouseDown && !middleMouseDown && io.MouseWheel != 0.0f;
        const bool blenderNavigationActive = blenderOrbitActive || blenderPanActive || blenderDollyDragActive;
        const bool startedBlenderNavigationThisFrame = blenderNavigationActive && !m_WasBlenderNavigationActive;
        m_WasBlenderNavigationActive = blenderNavigationActive;

        // Keep orbit target in sync when not actively navigating Blender-style.
        if (!blenderNavigationActive && !blenderWheelActive)
        {
            const glm::vec3 fwd = camera->GetForwardDirection();
            const float currentDistance = glm::length(m_OrbitTarget - camera->GetPosition());
            if (currentDistance > 0.001f)
                m_OrbitDistance = currentDistance;
            else
                m_OrbitDistance = std::max(0.25f, m_OrbitDistance);
            m_OrbitTarget = camera->GetPosition() + fwd * m_OrbitDistance;
        }

        const bool wantLook = rightMouseDown;
        const bool startedLookThisFrame = wantLook && !m_WasLookActive;
        m_WasLookActive = wantLook;

        const glm::vec2 move = wantLook ? m_ActionMove->ReadAxis2D() : glm::vec2(0.0f);
        const glm::vec2 look =
            ((wantLook && !startedLookThisFrame) || (blenderNavigationActive && !startedBlenderNavigationThisFrame))
            ? m_ActionLook->ReadAxis2D()
            : glm::vec2(0.0f);

        // =====================================================================
        // Blender-style navigation — immediate, no smoothing, no deltaTime.
        //
        // Matches Blender's turntable orbit model:
        //   - Orbit:  direct degrees-per-pixel rotation around pivot (turntable)
        //   - Pan:    view-plane translation proportional to pivot distance
        //   - Zoom:   multiplicative distance scaling (not additive)
        // =====================================================================
        if (blenderNavigationActive)
        {
            // Blender turntable: ~0.3 degrees per pixel at default sensitivity.
            // LookSensitivity is in radians-per-pixel; convert to degrees and
            // scale up slightly so orbit feels like Blender's turntable default.
            const float orbitDegreesPerUnit = LookSensitivity * (180.0f / 3.14159265f) * 2.0f;
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

            if (blenderOrbitActive)
            {
                // Turntable: yaw around world-up, pitch around camera-right.
                // Immediate — no interpolation.
                const float yaw = camera->GetYawDegrees() + (look.x * orbitDegreesPerUnit);
                const float pitch = camera->GetPitchDegrees() + (-look.y * orbitDegreesPerUnit);
                camera->SetYawPitchDegrees(yaw, pitch);
                // Reposition camera on the orbit sphere — pivot stays fixed.
                camera->SetPosition(m_OrbitTarget - camera->GetForwardDirection() * std::max(0.1f, m_OrbitDistance));
            }
            else if (blenderPanActive)
            {
                // Pan: translate pivot + camera along the view plane.
                // Speed is proportional to distance so it feels consistent
                // at any zoom level — just like Blender.
                glm::vec3 right = glm::cross(camera->GetForwardDirection(), worldUp);
                if (glm::length(right) < 0.0001f)
                    right = glm::vec3(1.0f, 0.0f, 0.0f);
                right = glm::normalize(right);
                const glm::vec3 up = glm::normalize(glm::cross(right, camera->GetForwardDirection()));

                const float panSpeed = std::max(0.01f, m_OrbitDistance) * 0.002f;
                const glm::vec3 panOffset = ((-right * look.x) - (up * look.y)) * panSpeed;
                m_OrbitTarget += panOffset;
                camera->SetPosition(camera->GetPosition() + panOffset);
            }
            else if (blenderDollyDragActive)
            {
                // Drag-dolly: multiplicative zoom from vertical mouse movement.
                // Each pixel multiplies distance by a small factor — feels
                // logarithmic like Blender's dolly zoom style.
                const float zoomFactor = std::pow(1.005f, look.y);
                m_OrbitDistance = std::max(0.1f, m_OrbitDistance * zoomFactor);
                camera->SetPosition(m_OrbitTarget - camera->GetForwardDirection() * m_OrbitDistance);
            }

            return;
        }

        // Scroll-wheel zoom: multiplicative, each notch ±10%.
        // Immediate — no smoothing. Matches Blender's wheel zoom.
        if (blenderWheelActive)
        {
            const float zoomFactor = std::pow(1.1f, -io.MouseWheel);
            m_OrbitDistance = std::max(0.1f, m_OrbitDistance * zoomFactor);
            camera->SetPosition(m_OrbitTarget - camera->GetForwardDirection() * m_OrbitDistance);
            return;
        }

        const float yaw = camera->GetYawDegrees() + (look.x * (LookSensitivity * 180.0f / 3.14159265f));
        const float pitch = camera->GetPitchDegrees() + (-look.y * (LookSensitivity * 180.0f / 3.14159265f));
        camera->SetYawPitchDegrees(yaw, pitch);

        const float boost = (m_ActionBoost && m_ActionBoost->ReadButton()) ? BoostMultiplier : 1.0f;
        const float speed = MoveSpeed * boost;

        const glm::vec3 forward = camera->GetForwardDirection();
        glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(right) < 0.0001f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        right = glm::normalize(right);

        glm::vec3 position = camera->GetPosition();
        position += forward * (move.y * speed * deltaTime);
        position += right * (move.x * speed * deltaTime);
        camera->SetPosition(position);
        m_OrbitTarget = position + camera->GetForwardDirection() * std::max(0.25f, m_OrbitDistance);
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

    void EditorCameraController::FocusOnPoint(const glm::vec3& point, float distance)
    {
        if (!m_CameraManager || !m_CameraId)
            return;

        auto* camera = m_CameraManager->GetPerspective3D(m_CameraId);
        if (!camera)
            return;

        float focusDistance = distance;
        if (focusDistance <= 0.0f)
            focusDistance = glm::length(point - camera->GetPosition());

        m_OrbitTarget = point;
        m_OrbitDistance = std::max(0.1f, focusDistance);
        camera->SetPosition(m_OrbitTarget - camera->GetForwardDirection() * m_OrbitDistance);
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

