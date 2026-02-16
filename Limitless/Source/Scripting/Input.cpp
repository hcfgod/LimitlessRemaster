#include "Scripting/Input.h"

namespace Limitless
{
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
}
