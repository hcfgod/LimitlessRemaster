#pragma once

#include "Core/Input/InputAction.h"

#include <glm/glm.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Limitless
{
    namespace Assets
    {
        class InputActionsAssetResource;
    }

    class InputRebinding;

    class InputSystem final
    {
    public:
        static InputSystem& GetInstance();

        InputSystem() = default;
        ~InputSystem() = default;

        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        void BeginFrame();

        // Feed raw SDL events.
        void OnSdlEvent(const SDL_Event& event);

        // Evaluate enabled action maps (should be called once per frame after events are pumped).
        void UpdateActions();

        // Unity-style ownership model:
        // - Project-wide default action asset (used when there are no overrides).
        // - Optional override stack (editor/gameplay layers can temporarily override without affecting the project).
        void SetProjectActionAsset(std::shared_ptr<InputActionAsset> asset) { m_ProjectActionAsset = std::move(asset); }
        std::shared_ptr<InputActionAsset> GetProjectActionAsset() const { return m_ProjectActionAsset; }

        void PushOverrideActionAsset(std::shared_ptr<InputActionAsset> asset);
        bool PopOverrideActionAsset(); // Pops the most recent override (if any).
        bool PopOverrideActionAsset(const std::shared_ptr<InputActionAsset>& expectedTop); // Safer pop.

        std::shared_ptr<InputActionAsset> GetActiveActionAsset() const;

        // Unity-style convenience: set project-wide input actions from an asset key.
        // (Loads through the engine asset system.)
        void SetProjectActionAssetFromKey(const std::string& key);

        // Additional project-level input action assets (Unity-style: multiple action assets can coexist).
        // These are addressable by key from gameplay/editor code.
        void SetProjectAdditionalActionAssetsFromKeys(const std::vector<std::string>& keys);
        const std::vector<std::string>& GetProjectAdditionalActionAssetKeys() const { return m_AdditionalProjectActionAssetKeys; }
        std::shared_ptr<InputActionAsset> GetProjectAdditionalActionAssetByKey(const std::string& key) const;

        // Backward compatible aliases (previous API was "global action asset").
        void SetActionAsset(std::shared_ptr<InputActionAsset> asset) { SetProjectActionAsset(std::move(asset)); }
        std::shared_ptr<InputActionAsset> GetActionAsset() const { return GetProjectActionAsset(); }

        // Device state polling
        bool IsKeyDown(SDL_Scancode scancode) const;
        bool WasKeyPressedThisFrame(SDL_Scancode scancode) const;
        bool WasKeyReleasedThisFrame(SDL_Scancode scancode) const;

        bool IsMouseButtonDown(uint8_t button) const;
        bool WasMouseButtonPressedThisFrame(uint8_t button) const;
        bool WasMouseButtonReleasedThisFrame(uint8_t button) const;

        glm::vec2 GetMousePosition() const { return m_MousePosition; }
        glm::vec2 GetMouseDelta() const { return m_MouseDelta; }
        glm::vec2 GetMouseWheelDelta() const { return m_MouseWheelDelta; }

        // Gamepad: explicit player/device indexing for multi-player support.
        static constexpr int kMaxGamepads = 4;

        int GetGamepadCount() const;
        bool HasGamepad(int playerIndex = 0) const;
        bool IsGamepadButtonDown(SDL_GamepadButton button) const { return IsGamepadButtonDown(0, button); }
        bool IsGamepadButtonDown(int playerIndex, SDL_GamepadButton button) const;
        bool WasGamepadButtonPressedThisFrame(SDL_GamepadButton button) const { return WasGamepadButtonPressedThisFrame(0, button); }
        bool WasGamepadButtonPressedThisFrame(int playerIndex, SDL_GamepadButton button) const;
        bool WasGamepadButtonReleasedThisFrame(SDL_GamepadButton button) const { return WasGamepadButtonReleasedThisFrame(0, button); }
        bool WasGamepadButtonReleasedThisFrame(int playerIndex, SDL_GamepadButton button) const;
        float GetGamepadAxis(SDL_GamepadAxis axis) const { return GetGamepadAxis(0, axis); }
        float GetGamepadAxis(int playerIndex, SDL_GamepadAxis axis) const;

        // Runtime rebinding support (optional).
        void SetRebindingSession(std::shared_ptr<InputRebinding> session) { m_RebindingSession = std::move(session); }
        std::shared_ptr<InputRebinding> GetRebindingSession() const { return m_RebindingSession; }

        // Unity-style input action polling helpers.
        bool IsActionPressed(std::string_view mapName, std::string_view actionName, float deadzone = 0.0001f) const;
        bool WasActionStartedThisFrame(std::string_view mapName, std::string_view actionName) const;
        bool WasActionPerformedThisFrame(std::string_view mapName, std::string_view actionName) const;
        bool WasActionCanceledThisFrame(std::string_view mapName, std::string_view actionName) const;
        bool ReadActionButton(std::string_view mapName, std::string_view actionName) const;
        float ReadActionAxis1D(std::string_view mapName, std::string_view actionName) const;
        glm::vec2 ReadActionAxis2D(std::string_view mapName, std::string_view actionName) const;
        bool HasAction(std::string_view mapName, std::string_view actionName) const;

    private:
        static constexpr size_t kMaxMouseButtons = 8;

        void OnKey(SDL_Scancode scancode, bool down, bool repeat);
        void OnMouseMotion(float x, float y, float dx, float dy);
        void OnMouseButton(uint8_t button, bool down);
        void OnMouseWheel(float x, float y);

        void OnGamepadAdded(SDL_JoystickID which);
        void OnGamepadRemoved(SDL_JoystickID which);
        void OnGamepadAxis(SDL_JoystickID which, SDL_GamepadAxis axis, int16_t value);
        void OnGamepadButton(SDL_JoystickID which, SDL_GamepadButton button, bool down);
        const InputAction* FindAction(std::string_view mapName, std::string_view actionName, bool warnIfMissing) const;
        void WarnMissingActionOnce(std::string_view mapName, std::string_view actionName) const;

        struct PerGamepadState
        {
            SDL_JoystickID Id = 0;
            SDL_Gamepad* Gamepad = nullptr;
            std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> Axis{};
            std::array<uint8_t, SDL_GAMEPAD_BUTTON_COUNT> ButtonDown{};
            std::array<uint8_t, SDL_GAMEPAD_BUTTON_COUNT> ButtonPressedThisFrame{};
            std::array<uint8_t, SDL_GAMEPAD_BUTTON_COUNT> ButtonReleasedThisFrame{};
        };
        static size_t FindSlotByGamepadId(std::array<PerGamepadState, kMaxGamepads>& gamepads, SDL_JoystickID id);
        static size_t FindFreeGamepadSlot(std::array<PerGamepadState, kMaxGamepads>& gamepads);

        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyDown{};
        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyPressedThisFrame{};
        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyReleasedThisFrame{};

        std::array<uint8_t, kMaxMouseButtons> m_MouseDown{};
        std::array<uint8_t, kMaxMouseButtons> m_MousePressedThisFrame{};
        std::array<uint8_t, kMaxMouseButtons> m_MouseReleasedThisFrame{};

        glm::vec2 m_MousePosition{0.0f, 0.0f};
        glm::vec2 m_MouseDelta{0.0f, 0.0f};
        glm::vec2 m_MouseWheelDelta{0.0f, 0.0f};

        std::array<PerGamepadState, kMaxGamepads> m_Gamepads{};

        std::shared_ptr<InputActionAsset> m_ProjectActionAsset;
        std::vector<std::shared_ptr<InputActionAsset>> m_ActionAssetOverrideStack;
        std::vector<std::string> m_AdditionalProjectActionAssetKeys;
        std::unordered_map<std::string, std::shared_ptr<InputActionAsset>> m_AdditionalProjectActionAssetsByKey;
        std::shared_ptr<Assets::InputActionsAssetResource> m_ProjectActionAssetResource;
        std::unordered_map<std::string, std::shared_ptr<Assets::InputActionsAssetResource>> m_AdditionalProjectActionAssetResourcesByKey;
        mutable std::unordered_set<std::string> m_WarnedMissingActions;

        std::shared_ptr<InputRebinding> m_RebindingSession;
    };

    inline InputSystem& GetInputSystem() { return InputSystem::GetInstance(); }
}

