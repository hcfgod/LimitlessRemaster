#pragma once

#include <glm/glm.hpp>

#include <string_view>

namespace Limitless
{
    using InputActionPressedBridgeCallback = bool (*)(const char* mapName, const char* actionName, float deadzone);
    using InputActionTriggerBridgeCallback = bool (*)(const char* mapName, const char* actionName);
    using InputActionAxis1DBridgeCallback = float (*)(const char* mapName, const char* actionName);
    using InputActionAxis2DBridgeCallback = glm::vec2 (*)(const char* mapName, const char* actionName);
    using InputActionExistsBridgeCallback = bool (*)(const char* mapName, const char* actionName);

    class InputActions final
    {
    public:
        InputActions() = delete;

        // Bridge wiring used by ScriptCore host/runtime.
        static void SetPressedBridgeCallback(InputActionPressedBridgeCallback callback);
        static void SetStartedBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetPerformedBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetCanceledBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetButtonBridgeCallback(InputActionTriggerBridgeCallback callback);
        static void SetAxis1DBridgeCallback(InputActionAxis1DBridgeCallback callback);
        static void SetAxis2DBridgeCallback(InputActionAxis2DBridgeCallback callback);
        static void SetExistsBridgeCallback(InputActionExistsBridgeCallback callback);

        // Unity-style InputAction polling API (name-based lookup).
        static bool IsPressed(std::string_view mapName, std::string_view actionName, float deadzone = 0.0001f);
        static bool WasStartedThisFrame(std::string_view mapName, std::string_view actionName);
        static bool WasPerformedThisFrame(std::string_view mapName, std::string_view actionName);
        static bool WasCanceledThisFrame(std::string_view mapName, std::string_view actionName);
        static bool ReadButton(std::string_view mapName, std::string_view actionName);
        static float ReadAxis1D(std::string_view mapName, std::string_view actionName);
        static glm::vec2 ReadAxis2D(std::string_view mapName, std::string_view actionName);
        static bool HasAction(std::string_view mapName, std::string_view actionName);
    };
}
