#include "Core/Input/InputSystem.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"

#include "Core/Input/InputRebinding.h"

#include <algorithm>

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

        m_GamepadButtonPressedThisFrame.fill(0);
        m_GamepadButtonReleasedThisFrame.fill(0);

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

    bool InputSystem::IsGamepadButtonDown(SDL_GamepadButton button) const
    {
        if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        {
            return false;
        }
        return m_GamepadButtonDown[static_cast<size_t>(button)] != 0;
    }

    bool InputSystem::WasGamepadButtonPressedThisFrame(SDL_GamepadButton button) const
    {
        if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        {
            return false;
        }
        return m_GamepadButtonPressedThisFrame[static_cast<size_t>(button)] != 0;
    }

    bool InputSystem::WasGamepadButtonReleasedThisFrame(SDL_GamepadButton button) const
    {
        if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        {
            return false;
        }
        return m_GamepadButtonReleasedThisFrame[static_cast<size_t>(button)] != 0;
    }

    float InputSystem::GetGamepadAxis(SDL_GamepadAxis axis) const
    {
        if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
        {
            return 0.0f;
        }

        const int16_t v = m_GamepadAxis[static_cast<size_t>(axis)];

        // Triggers are typically [0..32767], sticks are [-32768..32767].
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
        {
            return std::clamp(static_cast<float>(v) / 32767.0f, 0.0f, 1.0f);
        }
        return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f);
    }

    void InputSystem::OnGamepadAdded(SDL_JoystickID which)
    {
        if (m_Gamepad != nullptr)
        {
            // Keep current primary gamepad.
            return;
        }

        if (!SDL_IsGamepad(which))
        {
            return;
        }

        SDL_Gamepad* opened = SDL_OpenGamepad(which);
        if (!opened)
        {
            LT_CORE_WARN("InputSystem: SDL_OpenGamepad failed (which={})", static_cast<int>(which));
            return;
        }

        m_GamepadId = which;
        m_Gamepad = opened;
        m_GamepadAxis.fill(0);
        m_GamepadButtonDown.fill(0);
        m_GamepadButtonPressedThisFrame.fill(0);
        m_GamepadButtonReleasedThisFrame.fill(0);

        const char* name = SDL_GetGamepadName(m_Gamepad);
        LT_INFO("InputSystem: gamepad connected (id={}, name='{}')", static_cast<int>(which), (name ? name : "Unknown"));
    }

    void InputSystem::OnGamepadRemoved(SDL_JoystickID which)
    {
        if (m_Gamepad == nullptr || which != m_GamepadId)
        {
            return;
        }

        SDL_CloseGamepad(m_Gamepad);
        m_Gamepad = nullptr;
        m_GamepadId = 0;
        m_GamepadAxis.fill(0);
        m_GamepadButtonDown.fill(0);
        m_GamepadButtonPressedThisFrame.fill(0);
        m_GamepadButtonReleasedThisFrame.fill(0);

        LT_INFO("InputSystem: gamepad disconnected (id={})", static_cast<int>(which));
    }

    void InputSystem::OnGamepadAxis(SDL_JoystickID which, SDL_GamepadAxis axis, int16_t value)
    {
        if (m_Gamepad == nullptr || which != m_GamepadId)
        {
            return;
        }

        if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
        {
            return;
        }

        m_GamepadAxis[static_cast<size_t>(axis)] = value;
    }

    void InputSystem::OnGamepadButton(SDL_JoystickID which, SDL_GamepadButton button, bool down)
    {
        if (m_Gamepad == nullptr || which != m_GamepadId)
        {
            return;
        }

        if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        {
            return;
        }

        const size_t idx = static_cast<size_t>(button);
        const bool wasDown = (m_GamepadButtonDown[idx] != 0);
        m_GamepadButtonDown[idx] = down ? 1 : 0;

        if (!wasDown && down)
        {
            m_GamepadButtonPressedThisFrame[idx] = 1;
        }
        else if (wasDown && !down)
        {
            m_GamepadButtonReleasedThisFrame[idx] = 1;
        }
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

