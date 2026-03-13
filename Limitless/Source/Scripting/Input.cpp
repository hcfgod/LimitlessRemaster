#include "Scripting/Input.h"

#ifndef SCRIPTCORE_EXPORTS
    #include "Core/Input/InputSystem.h"
#endif

namespace Limitless
{
    namespace
    {
        InputMouseVector2BridgeCallback s_MousePositionBridgeCallback = nullptr;
        InputMouseVector2BridgeCallback s_MouseDeltaBridgeCallback = nullptr;
        InputMouseVector2BridgeCallback s_MouseWheelDeltaBridgeCallback = nullptr;
        InputMouseButtonBridgeCallback s_MouseButtonBridgeCallback = nullptr;
        InputMouseButtonBridgeCallback s_MouseButtonDownBridgeCallback = nullptr;
        InputMouseButtonBridgeCallback s_MouseButtonUpBridgeCallback = nullptr;
    }

    void Input::SetAxisBridgeCallback(InputActionAxis1DBridgeCallback callback)
    {
        InputActions::SetAxis1DBridgeCallback(callback);
    }

    void Input::SetButtonDownBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        InputActions::SetStartedBridgeCallback(callback);
    }

    void Input::SetButtonBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        InputActions::SetButtonBridgeCallback(callback);
    }

    void Input::SetMousePositionBridgeCallback(InputMouseVector2BridgeCallback callback)
    {
        s_MousePositionBridgeCallback = callback;
    }

    void Input::SetMouseDeltaBridgeCallback(InputMouseVector2BridgeCallback callback)
    {
        s_MouseDeltaBridgeCallback = callback;
    }

    void Input::SetMouseWheelDeltaBridgeCallback(InputMouseVector2BridgeCallback callback)
    {
        s_MouseWheelDeltaBridgeCallback = callback;
    }

    void Input::SetMouseButtonBridgeCallback(InputMouseButtonBridgeCallback callback)
    {
        s_MouseButtonBridgeCallback = callback;
    }

    void Input::SetMouseButtonDownBridgeCallback(InputMouseButtonBridgeCallback callback)
    {
        s_MouseButtonDownBridgeCallback = callback;
    }

    void Input::SetMouseButtonUpBridgeCallback(InputMouseButtonBridgeCallback callback)
    {
        s_MouseButtonUpBridgeCallback = callback;
    }

    float Input::GetAxis(const std::string_view mapName, const std::string_view actionName)
    {
        return InputActions::ReadAxis1D(mapName, actionName);
    }

    bool Input::GetButtonDown(const std::string_view mapName, const std::string_view actionName)
    {
        return InputActions::WasStartedThisFrame(mapName, actionName);
    }

    bool Input::GetButton(const std::string_view mapName, const std::string_view actionName)
    {
        return InputActions::ReadButton(mapName, actionName);
    }

    glm::vec2 Input::GetMousePosition()
    {
        if (s_MousePositionBridgeCallback)
            return s_MousePositionBridgeCallback();
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().GetMousePosition();
#else
        return glm::vec2(0.0f);
#endif
    }

    glm::vec2 Input::GetMouseDelta()
    {
        if (s_MouseDeltaBridgeCallback)
            return s_MouseDeltaBridgeCallback();
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().GetMouseDelta();
#else
        return glm::vec2(0.0f);
#endif
    }

    glm::vec2 Input::GetMouseWheelDelta()
    {
        if (s_MouseWheelDeltaBridgeCallback)
            return s_MouseWheelDeltaBridgeCallback();
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().GetMouseWheelDelta();
#else
        return glm::vec2(0.0f);
#endif
    }

    bool Input::GetMouseButton(MouseButton button)
    {
        const uint8_t rawButton = static_cast<uint8_t>(button);
        if (s_MouseButtonBridgeCallback)
            return s_MouseButtonBridgeCallback(rawButton);
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().IsMouseButtonDown(rawButton);
#else
        return false;
#endif
    }

    bool Input::GetMouseButtonDown(MouseButton button)
    {
        const uint8_t rawButton = static_cast<uint8_t>(button);
        if (s_MouseButtonDownBridgeCallback)
            return s_MouseButtonDownBridgeCallback(rawButton);
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().WasMouseButtonPressedThisFrame(rawButton);
#else
        return false;
#endif
    }

    bool Input::GetMouseButtonUp(MouseButton button)
    {
        const uint8_t rawButton = static_cast<uint8_t>(button);
        if (s_MouseButtonUpBridgeCallback)
            return s_MouseButtonUpBridgeCallback(rawButton);
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().WasMouseButtonReleasedThisFrame(rawButton);
#else
        return false;
#endif
    }
}
