#pragma once

#include "Scripting/InputActions.h"

namespace Limitless
{
    class Input final
    {
    public:
        Input() = delete;

        // Bridge wiring (used by ScriptCore host/runtime).
        static void SetAxisBridgeCallback(InputActionAxis1DBridgeCallback callback);
        static void SetButtonDownBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetButtonBridgeCallback(InputActionTriggerBridgeCallback callback);

        // Unity-style script API.
        static float GetAxis(const std::string_view mapName, const std::string_view actionName);
        static bool GetButtonDown(const std::string_view mapName, const std::string_view actionName);
        static bool GetButton(const std::string_view mapName, const std::string_view actionName);
    };
}
