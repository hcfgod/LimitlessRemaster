#pragma once

#include "Limitless.h"

#include <memory>
#include <string>

namespace Limitless
{
    namespace Assets
    {
        class InputActionsAssetResource;
    }

    // -----------------------------------------------------------------------------
    // EditorCameraController
    // Reusable editor-style free-look camera controller for tools/editor runtime:
    // - WASD moves (keyboard)
    // - Mouse delta controls look (when LookEnable is held, e.g., RMB)
    // - Shift boosts movement speed
    // - Optional gamepad bindings can be provided via the same actions asset.
    //
    // The controller uses an InputActionAsset override (Unity-style) so editor input
    // never overwrites the project-wide gameplay input actions.
    // -----------------------------------------------------------------------------
    class EditorCameraController final
    {
    public:
        struct Settings
        {
            // Unity-style input actions asset file containing an "Editor" map with:
            // - Move (Axis2D)
            // - Look (Axis2D)
            // - Boost (Button)
            // - LookEnable (Button)
            std::string InputActionsAssetKey;

            // If true, the controller pushes an override asset while active.
            bool UseOverrideActionAsset;

            Settings()
                : InputActionsAssetKey("Assets/InputActions/EditorCamera.inputactions.json")
                , UseOverrideActionAsset(true)
            {
            }
        };

        EditorCameraController() = default;
        ~EditorCameraController() = default;

        void Initialize(CameraManager& cameraManager, CameraId cameraId, Settings settings = Settings{});
        void Shutdown();

        void Update(float deltaTime);
        void OnWindowResize(uint32_t widthPixels, uint32_t heightPixels);

        /// When false, input is ignored and cursor is restored. Use when viewport is not focused.
        void SetInputEnabled(bool enabled);
        bool IsInputEnabled() const { return m_InputEnabled; }

        // Expose action debug for tooling
        const InputAction* GetMoveAction() const { return m_ActionMove; }
        const InputAction* GetLookAction() const { return m_ActionLook; }
        const InputAction* GetBoostAction() const { return m_ActionBoost; }
        const InputAction* GetLookEnableAction() const { return m_ActionLookEnable; }

        float MoveSpeed = 3.0f;
        float BoostMultiplier = 4.0f;
        float LookSensitivity = 0.0025f;

    private:
        void EnsureInputAsset();
        void RefreshInputAssetIfHotReloaded();

        Settings m_Settings{};

        std::shared_ptr<InputActionAsset> m_InputAsset;
        std::shared_ptr<Assets::InputActionsAssetResource> m_InputAssetResource;
        uint64_t m_InputAssetRevision = 0;

        InputAction* m_ActionMove = nullptr;
        InputAction* m_ActionLook = nullptr;
        InputAction* m_ActionBoost = nullptr;
        InputAction* m_ActionLookEnable = nullptr;

        CameraManager* m_CameraManager = nullptr;
        CameraId m_CameraId{};
        bool m_InputEnabled = true;  ///< When false, Update ignores input and cursor is restored.
        bool m_WasLookActive = false;
    };
}

