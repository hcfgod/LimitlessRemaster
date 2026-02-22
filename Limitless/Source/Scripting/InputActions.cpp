#include "Scripting/InputActions.h"

#ifndef SCRIPTCORE_EXPORTS
    #include "Core/Input/InputSystem.h"
#endif

#include <string>

namespace Limitless
{
    namespace
    {
        InputActionPressedBridgeCallback s_PressedBridgeCallback = nullptr;
        InputActionTriggerBridgeCallback s_StartedBridgeCallback = nullptr;
        InputActionTriggerBridgeCallback s_PerformedBridgeCallback = nullptr;
        InputActionTriggerBridgeCallback s_CanceledBridgeCallback = nullptr;
        InputActionTriggerBridgeCallback s_ButtonBridgeCallback = nullptr;
        InputActionAxis1DBridgeCallback s_Axis1DBridgeCallback = nullptr;
        InputActionAxis2DBridgeCallback s_Axis2DBridgeCallback = nullptr;
        InputActionExistsBridgeCallback s_ExistsBridgeCallback = nullptr;

        static std::string ToOwned(std::string_view text)
        {
            return std::string(text);
        }
    }

    void InputActions::SetPressedBridgeCallback(InputActionPressedBridgeCallback callback)
    {
        s_PressedBridgeCallback = callback;
    }

    void InputActions::SetStartedBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        s_StartedBridgeCallback = callback;
    }

    void InputActions::SetPerformedBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        s_PerformedBridgeCallback = callback;
    }

    void InputActions::SetCanceledBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        s_CanceledBridgeCallback = callback;
    }

    void InputActions::SetButtonBridgeCallback(InputActionTriggerBridgeCallback callback)
    {
        s_ButtonBridgeCallback = callback;
    }

    void InputActions::SetAxis1DBridgeCallback(InputActionAxis1DBridgeCallback callback)
    {
        s_Axis1DBridgeCallback = callback;
    }

    void InputActions::SetAxis2DBridgeCallback(InputActionAxis2DBridgeCallback callback)
    {
        s_Axis2DBridgeCallback = callback;
    }

    void InputActions::SetExistsBridgeCallback(InputActionExistsBridgeCallback callback)
    {
        s_ExistsBridgeCallback = callback;
    }

    bool InputActions::IsPressed(std::string_view mapName, std::string_view actionName, float deadzone)
    {
        if (s_PressedBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_PressedBridgeCallback(mapNameText.c_str(), actionNameText.c_str(), deadzone);
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().IsActionPressed(mapName, actionName, deadzone);
#else
        return false;
#endif
    }

    bool InputActions::WasStartedThisFrame(std::string_view mapName, std::string_view actionName)
    {
        if (s_StartedBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_StartedBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().WasActionStartedThisFrame(mapName, actionName);
#else
        return false;
#endif
    }

    bool InputActions::WasPerformedThisFrame(std::string_view mapName, std::string_view actionName)
    {
        if (s_PerformedBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_PerformedBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().WasActionPerformedThisFrame(mapName, actionName);
#else
        return false;
#endif
    }

    bool InputActions::WasCanceledThisFrame(std::string_view mapName, std::string_view actionName)
    {
        if (s_CanceledBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_CanceledBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().WasActionCanceledThisFrame(mapName, actionName);
#else
        return false;
#endif
    }

    bool InputActions::ReadButton(std::string_view mapName, std::string_view actionName)
    {
        if (s_ButtonBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_ButtonBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().ReadActionButton(mapName, actionName);
#else
        return false;
#endif
    }

    float InputActions::ReadAxis1D(std::string_view mapName, std::string_view actionName)
    {
        if (s_Axis1DBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_Axis1DBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().ReadActionAxis1D(mapName, actionName);
#else
        return 0.0f;
#endif
    }

    glm::vec2 InputActions::ReadAxis2D(std::string_view mapName, std::string_view actionName)
    {
        if (s_Axis2DBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_Axis2DBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().ReadActionAxis2D(mapName, actionName);
#else
        return glm::vec2(0.0f);
#endif
    }

    bool InputActions::HasAction(std::string_view mapName, std::string_view actionName)
    {
        if (s_ExistsBridgeCallback)
        {
            const std::string mapNameText = ToOwned(mapName);
            const std::string actionNameText = ToOwned(actionName);
            return s_ExistsBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
        }
#ifndef SCRIPTCORE_EXPORTS
        return InputSystem::GetInstance().HasAction(mapName, actionName);
#else
        return false;
#endif
    }
}
