#include "Core/Input/InputSystem.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"

#include "Core/Input/InputRebinding.h"

#include <algorithm>
#include <optional>

namespace Limitless
{
    InputSystem& InputSystem::GetInstance()
    {
        static InputSystem instance;
        return instance;
    }

    void InputSystem::BeginFrame()
    {
        m_KeyPressedThisFrame.fill(0);
        m_KeyReleasedThisFrame.fill(0);

        m_MousePressedThisFrame.fill(0);
        m_MouseReleasedThisFrame.fill(0);

        for (PerGamepadState& pad : m_Gamepads)
        {
            if (pad.Gamepad)
            {
                pad.ButtonPressedThisFrame.fill(0);
                pad.ButtonReleasedThisFrame.fill(0);
            }
        }

        m_MouseDelta = glm::vec2(0.0f);
        m_MouseWheelDelta = glm::vec2(0.0f);
    }

    void InputSystem::OnSdlEvent(const SDL_Event& event)
    {
        // Optional: allow an active rebinding session to capture the next input.
        if (m_RebindingSession && m_RebindingSession->IsActive())
        {
            if (m_RebindingSession->TryConsumeEvent(event))
            {
                return;
            }
        }

        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                OnKey(event.key.scancode, event.key.down, event.key.repeat);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                OnMouseMotion(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                OnMouseButton(event.button.button, event.button.down);
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                OnMouseWheel(event.wheel.x, event.wheel.y);
                break;

            case SDL_EVENT_GAMEPAD_ADDED:
                OnGamepadAdded(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                OnGamepadRemoved(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                OnGamepadAxis(event.gaxis.which, static_cast<SDL_GamepadAxis>(event.gaxis.axis), event.gaxis.value);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                OnGamepadButton(event.gbutton.which, static_cast<SDL_GamepadButton>(event.gbutton.button), event.gbutton.down);
                break;
        }
    }

    void InputSystem::UpdateActions()
    {
        if (auto asset = GetActiveActionAsset())
        {
            asset->Update(*this);
        }
    }

    void InputSystem::PushOverrideActionAsset(std::shared_ptr<InputActionAsset> asset)
    {
        if (!asset)
        {
            LT_CORE_WARN("InputSystem: PushOverrideActionAsset called with null asset (ignored)");
            return;
        }

        m_ActionAssetOverrideStack.push_back(std::move(asset));
    }

    bool InputSystem::PopOverrideActionAsset()
    {
        if (m_ActionAssetOverrideStack.empty())
        {
            return false;
        }

        m_ActionAssetOverrideStack.pop_back();
        return true;
    }

    bool InputSystem::PopOverrideActionAsset(const std::shared_ptr<InputActionAsset>& expectedTop)
    {
        if (m_ActionAssetOverrideStack.empty())
        {
            return false;
        }

        if (expectedTop && m_ActionAssetOverrideStack.back() != expectedTop)
        {
            LT_CORE_WARN("InputSystem: override asset pop mismatch (expected top does not match). Not popping.");
            return false;
        }

        m_ActionAssetOverrideStack.pop_back();
        return true;
    }

    std::shared_ptr<InputActionAsset> InputSystem::GetActiveActionAsset() const
    {
        if (!m_ActionAssetOverrideStack.empty())
        {
            return m_ActionAssetOverrideStack.back();
        }
        return m_ProjectActionAsset;
    }

    void InputSystem::SetProjectActionAssetFromKey(const std::string& key)
    {
        auto resource = Assets::AssetManager::LoadBlocking<Assets::InputActionsAssetResource>(key);
        if (!resource || !resource->GetValue())
        {
            LT_CORE_ERROR("InputSystem: failed to load InputActions asset key='{}'", key);
            return;
        }

        // Keep the resource alive so in-place reloads propagate to this shared InputActionAsset.
        m_ProjectActionAssetResource = resource;
        SetProjectActionAsset(resource->GetValue());
    }

    void InputSystem::SetProjectAdditionalActionAssetsFromKeys(const std::vector<std::string>& keys)
    {
        m_AdditionalProjectActionAssetKeys.clear();
        m_AdditionalProjectActionAssetsByKey.clear();
        m_AdditionalProjectActionAssetResourcesByKey.clear();

        if (keys.empty())
            return;

        std::vector<std::string> deduplicatedKeys = keys;
        std::sort(deduplicatedKeys.begin(), deduplicatedKeys.end());
        deduplicatedKeys.erase(std::unique(deduplicatedKeys.begin(), deduplicatedKeys.end()), deduplicatedKeys.end());

        m_AdditionalProjectActionAssetKeys.reserve(deduplicatedKeys.size());
        for (const std::string& key : deduplicatedKeys)
        {
            if (key.empty())
                continue;

            auto resource = Assets::AssetManager::LoadBlocking<Assets::InputActionsAssetResource>(key);
            if (!resource || !resource->GetValue())
            {
                LT_CORE_WARN("InputSystem: failed to load additional InputActions asset key='{}'", key);
                continue;
            }

            m_AdditionalProjectActionAssetKeys.push_back(key);
            m_AdditionalProjectActionAssetsByKey[key] = resource->GetValue();
            m_AdditionalProjectActionAssetResourcesByKey[key] = std::move(resource);
        }
    }

    std::shared_ptr<InputActionAsset> InputSystem::GetProjectAdditionalActionAssetByKey(const std::string& key) const
    {
        const auto it = m_AdditionalProjectActionAssetsByKey.find(key);
        if (it == m_AdditionalProjectActionAssetsByKey.end())
            return nullptr;
        return it->second;
    }

    bool InputSystem::IsKeyDown(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyDown[scancode] != 0;
    }

    bool InputSystem::WasKeyPressedThisFrame(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyPressedThisFrame[scancode] != 0;
    }

    bool InputSystem::WasKeyReleasedThisFrame(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyReleasedThisFrame[scancode] != 0;
    }

    bool InputSystem::IsMouseButtonDown(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MouseDown[button] != 0;
    }

    bool InputSystem::WasMouseButtonPressedThisFrame(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MousePressedThisFrame[button] != 0;
    }

    bool InputSystem::WasMouseButtonReleasedThisFrame(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MouseReleasedThisFrame[button] != 0;
    }

    int InputSystem::GetGamepadCount() const
    {
        int n = 0;
        for (const PerGamepadState& pad : m_Gamepads)
        {
            if (pad.Gamepad != nullptr)
                ++n;
        }
        return n;
    }

    bool InputSystem::HasGamepad(int playerIndex) const
    {
        if (playerIndex < 0 || playerIndex >= kMaxGamepads)
            return false;
        return m_Gamepads[static_cast<size_t>(playerIndex)].Gamepad != nullptr;
    }

    bool InputSystem::IsGamepadButtonDown(int playerIndex, SDL_GamepadButton button) const
    {
        if (playerIndex < 0 || playerIndex >= kMaxGamepads || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
            return false;
        const PerGamepadState& pad = m_Gamepads[static_cast<size_t>(playerIndex)];
        return pad.Gamepad != nullptr && pad.ButtonDown[static_cast<size_t>(button)] != 0;
    }

    bool InputSystem::WasGamepadButtonPressedThisFrame(int playerIndex, SDL_GamepadButton button) const
    {
        if (playerIndex < 0 || playerIndex >= kMaxGamepads || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
            return false;
        const PerGamepadState& pad = m_Gamepads[static_cast<size_t>(playerIndex)];
        return pad.Gamepad != nullptr && pad.ButtonPressedThisFrame[static_cast<size_t>(button)] != 0;
    }

    bool InputSystem::WasGamepadButtonReleasedThisFrame(int playerIndex, SDL_GamepadButton button) const
    {
        if (playerIndex < 0 || playerIndex >= kMaxGamepads || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
            return false;
        const PerGamepadState& pad = m_Gamepads[static_cast<size_t>(playerIndex)];
        return pad.Gamepad != nullptr && pad.ButtonReleasedThisFrame[static_cast<size_t>(button)] != 0;
    }

    float InputSystem::GetGamepadAxis(int playerIndex, SDL_GamepadAxis axis) const
    {
        if (playerIndex < 0 || playerIndex >= kMaxGamepads || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
            return 0.0f;
        const PerGamepadState& pad = m_Gamepads[static_cast<size_t>(playerIndex)];
        if (pad.Gamepad == nullptr)
            return 0.0f;
        const int16_t v = pad.Axis[static_cast<size_t>(axis)];
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            return std::clamp(static_cast<float>(v) / 32767.0f, 0.0f, 1.0f);
        return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f);
    }

    size_t InputSystem::FindSlotByGamepadId(std::array<PerGamepadState, kMaxGamepads>& gamepads, SDL_JoystickID id)
    {
        for (size_t i = 0; i < gamepads.size(); ++i)
        {
            if (gamepads[i].Gamepad != nullptr && gamepads[i].Id == id)
            {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    size_t InputSystem::FindFreeGamepadSlot(std::array<PerGamepadState, kMaxGamepads>& gamepads)
    {
        for (size_t i = 0; i < gamepads.size(); ++i)
        {
            if (gamepads[i].Gamepad == nullptr)
            {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    void InputSystem::OnGamepadAdded(SDL_JoystickID which)
    {
        if (FindSlotByGamepadId(m_Gamepads, which) != static_cast<size_t>(-1))
            return; // already tracked

        if (!SDL_IsGamepad(which))
            return;

        const size_t slot = FindFreeGamepadSlot(m_Gamepads);
        if (slot == static_cast<size_t>(-1))
        {
            LT_CORE_WARN("InputSystem: no free gamepad slot (max={})", kMaxGamepads);
            return;
        }

        SDL_Gamepad* opened = SDL_OpenGamepad(which);
        if (!opened)
        {
            LT_CORE_WARN("InputSystem: SDL_OpenGamepad failed (which={})", static_cast<int>(which));
            return;
        }

        PerGamepadState& pad = m_Gamepads[slot];
        pad.Id = which;
        pad.Gamepad = opened;
        pad.Axis.fill(0);
        pad.ButtonDown.fill(0);
        pad.ButtonPressedThisFrame.fill(0);
        pad.ButtonReleasedThisFrame.fill(0);

        const char* name = SDL_GetGamepadName(pad.Gamepad);
        LT_INFO("InputSystem: gamepad connected (playerIndex={}, id={}, name='{}')", static_cast<int>(slot), static_cast<int>(which), (name ? name : "Unknown"));
    }

    void InputSystem::OnGamepadRemoved(SDL_JoystickID which)
    {
        const size_t slot = FindSlotByGamepadId(m_Gamepads, which);
        if (slot == static_cast<size_t>(-1))
            return;

        PerGamepadState& pad = m_Gamepads[slot];
        SDL_CloseGamepad(pad.Gamepad);
        pad.Id = 0;
        pad.Gamepad = nullptr;
        pad.Axis.fill(0);
        pad.ButtonDown.fill(0);
        pad.ButtonPressedThisFrame.fill(0);
        pad.ButtonReleasedThisFrame.fill(0);

        LT_INFO("InputSystem: gamepad disconnected (playerIndex={}, id={})", static_cast<int>(slot), static_cast<int>(which));
    }

    void InputSystem::OnGamepadAxis(SDL_JoystickID which, SDL_GamepadAxis axis, int16_t value)
    {
        const size_t slot = FindSlotByGamepadId(m_Gamepads, which);
        if (slot == static_cast<size_t>(-1) || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
            return;
        m_Gamepads[slot].Axis[static_cast<size_t>(axis)] = value;
    }

    void InputSystem::OnGamepadButton(SDL_JoystickID which, SDL_GamepadButton button, bool down)
    {
        const size_t slot = FindSlotByGamepadId(m_Gamepads, which);
        if (slot == static_cast<size_t>(-1) || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
            return;

        PerGamepadState& pad = m_Gamepads[slot];
        const size_t idx = static_cast<size_t>(button);
        const bool wasDown = (pad.ButtonDown[idx] != 0);
        pad.ButtonDown[idx] = down ? 1 : 0;
        if (!wasDown && down)
            pad.ButtonPressedThisFrame[idx] = 1;
        else if (wasDown && !down)
            pad.ButtonReleasedThisFrame[idx] = 1;
    }

    void InputSystem::OnKey(SDL_Scancode scancode, bool down, bool repeat)
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return;
        }

        const bool wasDown = (m_KeyDown[scancode] != 0);
        m_KeyDown[scancode] = down ? 1 : 0;

        if (!repeat)
        {
            if (!wasDown && down)
            {
                m_KeyPressedThisFrame[scancode] = 1;
            }
            if (wasDown && !down)
            {
                m_KeyReleasedThisFrame[scancode] = 1;
            }
        }
    }

    void InputSystem::OnMouseMotion(float x, float y, float dx, float dy)
    {
        m_MousePosition = glm::vec2(x, y);
        m_MouseDelta += glm::vec2(dx, dy);
    }

    void InputSystem::OnMouseButton(uint8_t button, bool down)
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return;
        }

        const bool wasDown = (m_MouseDown[button] != 0);
        m_MouseDown[button] = down ? 1 : 0;

        if (!wasDown && down)
        {
            m_MousePressedThisFrame[button] = 1;
        }
        if (wasDown && !down)
        {
            m_MouseReleasedThisFrame[button] = 1;
        }
    }

    void InputSystem::OnMouseWheel(float x, float y)
    {
        m_MouseWheelDelta += glm::vec2(x, y);
    }

    void InputSystem::WarnMissingActionOnce(std::string_view mapName, std::string_view actionName) const
    {
        std::string missingKey;
        missingKey.reserve(mapName.size() + actionName.size() + 2);
        missingKey.append(mapName);
        missingKey.append("::");
        missingKey.append(actionName);

        if (!m_WarnedMissingActions.insert(missingKey).second)
            return;

        LT_CORE_WARN(
            "InputSystem: action '{}::{}' is missing from the active input actions asset.",
            mapName,
            actionName);
    }

    const InputAction* InputSystem::FindAction(std::string_view mapName, std::string_view actionName, bool warnIfMissing) const
    {
        const auto asset = GetActiveActionAsset();
        if (!asset)
        {
            if (warnIfMissing)
                WarnMissingActionOnce(mapName, actionName);
            return nullptr;
        }
        const InputActionMap* map = asset->FindMap(mapName);
        if (!map)
        {
            if (warnIfMissing)
                WarnMissingActionOnce(mapName, actionName);
            return nullptr;
        }
        const InputAction* action = map->FindAction(actionName);
        if (!action && warnIfMissing)
            WarnMissingActionOnce(mapName, actionName);
        return action;
    }

    bool InputSystem::IsActionPressed(std::string_view mapName, std::string_view actionName, float deadzone) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->IsPressed(deadzone) : false;
    }

    bool InputSystem::WasActionStartedThisFrame(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->WasStartedThisFrame() : false;
    }

    bool InputSystem::WasActionPerformedThisFrame(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->WasPerformedThisFrame() : false;
    }

    bool InputSystem::WasActionCanceledThisFrame(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->WasCanceledThisFrame() : false;
    }

    bool InputSystem::ReadActionButton(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->ReadButton() : false;
    }

    float InputSystem::ReadActionAxis1D(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->ReadAxis1D() : 0.0f;
    }

    glm::vec2 InputSystem::ReadActionAxis2D(std::string_view mapName, std::string_view actionName) const
    {
        const InputAction* action = FindAction(mapName, actionName, true);
        return action ? action->ReadAxis2D() : glm::vec2(0.0f);
    }

    bool InputSystem::HasAction(std::string_view mapName, std::string_view actionName) const
    {
        return FindAction(mapName, actionName, true) != nullptr;
    }
}

