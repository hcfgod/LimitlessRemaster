#pragma once

#include "Limitless.h"

namespace Limitless
{
    namespace Assets
    {
        class InputActionsAssetResource;
    }

    // Small, reusable editor-style free-look camera controller:
    // - WASD moves
    // - RMB enables mouse-look and locks/hides cursor
    // - Shift boosts movement speed
    class EditorCameraController final
    {
    public:
        EditorCameraController() = default;
        ~EditorCameraController() = default;

        void Initialize(CameraManager& cameraManager, CameraId cameraId);
        void Shutdown();

        void Update(float deltaTime);
        void OnWindowResize(uint32_t widthPixels, uint32_t heightPixels);

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

        std::shared_ptr<InputActionAsset> m_InputAsset;
        std::shared_ptr<Assets::InputActionsAssetResource> m_InputAssetResource;
        InputAction* m_ActionMove = nullptr;
        InputAction* m_ActionLook = nullptr;
        InputAction* m_ActionBoost = nullptr;
        InputAction* m_ActionLookEnable = nullptr;

        CameraManager* m_CameraManager = nullptr;
        CameraId m_CameraId{};
    };
}

