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
#ifdef SCRIPTCORE_EXPORTS
        if (!s_PressedBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_PressedBridgeCallback(mapNameText.c_str(), actionNameText.c_str(), deadzone);
#else
        return InputSystem::GetInstance().IsActionPressed(mapName, actionName, deadzone);
#endif
    }

    bool InputActions::WasStartedThisFrame(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_StartedBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_StartedBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().WasActionStartedThisFrame(mapName, actionName);
#endif
    }

    bool InputActions::WasPerformedThisFrame(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_PerformedBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_PerformedBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().WasActionPerformedThisFrame(mapName, actionName);
#endif
    }

    bool InputActions::WasCanceledThisFrame(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_CanceledBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_CanceledBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().WasActionCanceledThisFrame(mapName, actionName);
#endif
    }

    bool InputActions::ReadButton(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_ButtonBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_ButtonBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().ReadActionButton(mapName, actionName);
#endif
    }

    float InputActions::ReadAxis1D(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_Axis1DBridgeCallback)
            return 0.0f;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_Axis1DBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().ReadActionAxis1D(mapName, actionName);
#endif
    }

    glm::vec2 InputActions::ReadAxis2D(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_Axis2DBridgeCallback)
            return glm::vec2(0.0f);
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_Axis2DBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().ReadActionAxis2D(mapName, actionName);
#endif
    }

    bool InputActions::HasAction(std::string_view mapName, std::string_view actionName)
    {
#ifdef SCRIPTCORE_EXPORTS
        if (!s_ExistsBridgeCallback)
            return false;
        const std::string mapNameText = ToOwned(mapName);
        const std::string actionNameText = ToOwned(actionName);
        return s_ExistsBridgeCallback(mapNameText.c_str(), actionNameText.c_str());
#else
        return InputSystem::GetInstance().HasAction(mapName, actionName);
#endif
    }
}
