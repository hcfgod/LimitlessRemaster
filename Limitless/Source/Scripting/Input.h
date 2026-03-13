#pragma once

#include "Scripting/InputActions.h"

#include <cstdint>

namespace Limitless
{
    using InputMouseButtonBridgeCallback = bool (*)(uint8_t button);
    using InputMouseVector2BridgeCallback = glm::vec2 (*)();

    enum class MouseButton : uint8_t
    {
        Left = 1,
        Middle = 2,
        Right = 3,
        X1 = 4,
        X2 = 5
    };

    class Input final
    {
    public:
        Input() = delete;

        // Bridge wiring (used by ScriptCore host/runtime).
        static void SetAxisBridgeCallback(InputActionAxis1DBridgeCallback callback);
        static void SetButtonDownBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetButtonBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetMousePositionBridgeCallback(InputMouseVector2BridgeCallback callback);
        static void SetMouseDeltaBridgeCallback(InputMouseVector2BridgeCallback callback);
        static void SetMouseWheelDeltaBridgeCallback(InputMouseVector2BridgeCallback callback);
        static void SetMouseButtonBridgeCallback(InputMouseButtonBridgeCallback callback);
        static void SetMouseButtonDownBridgeCallback(InputMouseButtonBridgeCallback callback);
        static void SetMouseButtonUpBridgeCallback(InputMouseButtonBridgeCallback callback);

        // Unity-style script API.
        static float GetAxis(const std::string_view mapName, const std::string_view actionName);
        static bool GetButtonDown(const std::string_view mapName, const std::string_view actionName);
        static bool GetButton(const std::string_view mapName, const std::string_view actionName);
        static glm::vec2 GetMousePosition();
        static glm::vec2 GetMouseDelta();
        static glm::vec2 GetMouseWheelDelta();
        static bool GetMouseButton(MouseButton button);
        static bool GetMouseButtonDown(MouseButton button);
        static bool GetMouseButtonUp(MouseButton button);
    };
}
