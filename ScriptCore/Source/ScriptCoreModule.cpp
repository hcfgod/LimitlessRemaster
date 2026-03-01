#include "ScriptCoreRegistration.h"
#include "Scene/SceneManager.h"
#include "Scripting/Debug.h"
#include "Scripting/Input.h"
#include "Scripting/InputActions.h"
#include "Scripting/Physics2D.h"

extern "C" LT_SCRIPTCORE_API uint32_t LT_GetScriptCoreAbiVersion()
{
    return Limitless::kScriptCoreAbiVersion;
}

extern "C" LT_SCRIPTCORE_API void LT_RegisterScriptCoreTypes(Limitless::NativeScriptRegistrationCallback registrationCallback)
{
    if (!registrationCallback)
        return;

    for (const auto& registration : Limitless::ScriptCore::GetRegistrations())
    {
        registrationCallback(registration.ClassName.c_str(), registration.CreateFunction);
    }
}

extern "C" LT_SCRIPTCORE_API void LT_SetSceneTransitionBridge(Limitless::SceneTransitionBridgeCallback callback)
{
    Limitless::SceneManager::SetTransitionBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionAxis1DBridge(Limitless::InputActionAxis1DBridgeCallback callback)
{
    Limitless::InputActions::SetAxis1DBridgeCallback(callback);
    Limitless::Input::SetAxisBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionAxis2DBridge(Limitless::InputActionAxis2DBridgeCallback callback)
{
    Limitless::InputActions::SetAxis2DBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionExistsBridge(Limitless::InputActionExistsBridgeCallback callback)
{
    Limitless::InputActions::SetExistsBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionPressedBridge(Limitless::InputActionPressedBridgeCallback callback)
{
    Limitless::InputActions::SetPressedBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionStartedBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetStartedBridgeCallback(callback);
    Limitless::Input::SetButtonDownBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionPerformedBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetPerformedBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionCanceledBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetCanceledBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputActionButtonBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetButtonBridgeCallback(callback);
    Limitless::Input::SetButtonBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputButtonDownBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::Input::SetButtonDownBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetInputButtonBridge(Limitless::InputActionTriggerBridgeCallback callback)
{
    Limitless::InputActions::SetButtonBridgeCallback(callback);
    Limitless::Input::SetButtonBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetPhysics2DRaycastBridge(Limitless::Physics2DRaycastBridgeCallback callback)
{
    Limitless::Physics2D::SetRaycastBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetScriptLogBridge(Limitless::ScriptLogBridgeCallback callback)
{
    Limitless::Debug::SetLogBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetScriptCreateEntityBridge(Limitless::ScriptCreateEntityBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetCreateEntityBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetScriptDestroyEntityBridge(Limitless::ScriptDestroyEntityBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetDestroyEntityBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetScriptInstantiatePrefabBridge(Limitless::ScriptInstantiatePrefabBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetInstantiatePrefabBridgeCallback(callback);
}

extern "C" LT_SCRIPTCORE_API void LT_SetScriptContactEntityHandlesBridge(Limitless::ScriptGetContactEntityHandlesBridgeCallback callback)
{
    Limitless::ScriptableEntity::SetContactEntityHandlesBridgeCallback(callback);
}
