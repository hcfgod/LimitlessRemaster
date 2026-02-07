#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// SDL scancodes are currently the engine-wide key identifier (physical keys).
// This keeps bindings stable across keyboard layouts.
#include <SDL3/SDL_scancode.h>

namespace Limitless
{
    class InputSystem;

    enum class InputActionValueType : uint8_t
    {
        Button = 0,
        Axis1D = 1,
        Axis2D = 2
    };

    enum class InputActionPhase : uint8_t
    {
        Disabled = 0,
        Waiting,
        Started,
        Performed,
        Canceled
    };

    class InputActionValue final
    {
    public:
        InputActionValue() = default;
        explicit InputActionValue(bool b) : m_Value(b) {}
        explicit InputActionValue(float f) : m_Value(f) {}
        explicit InputActionValue(const glm::vec2& v) : m_Value(v) {}

        InputActionValueType GetType() const { return m_Type; }

        bool AsButton() const;
        float AsAxis1D() const;
        glm::vec2 AsAxis2D() const;

        static InputActionValue Button(bool b);
        static InputActionValue Axis1D(float v);
        static InputActionValue Axis2D(const glm::vec2& v);

        bool IsActuated(float deadzone = 0.0001f) const;

    private:
        InputActionValueType m_Type = InputActionValueType::Button;
        std::variant<bool, float, glm::vec2> m_Value{false};
    };

    struct KeyboardButtonBinding
    {
        SDL_Scancode Key = SDL_SCANCODE_UNKNOWN;
    };

    struct MouseButtonBinding
    {
        uint8_t Button = 0; // SDL_BUTTON_LEFT/RIGHT/MIDDLE...
    };

    struct KeyboardAxis1DBinding
    {
        SDL_Scancode Negative = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode Positive = SDL_SCANCODE_UNKNOWN;
        float NegativeScale = -1.0f;
        float PositiveScale = 1.0f;
    };

    struct KeyboardAxis2DBinding
    {
        SDL_Scancode Up = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode Down = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode Left = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode Right = SDL_SCANCODE_UNKNOWN;
        float Scale = 1.0f;
    };

    struct MouseDeltaBinding
    {
        float Sensitivity = 1.0f;
        bool InvertY = false;
    };

    using InputBinding = std::variant<
        KeyboardButtonBinding,
        MouseButtonBinding,
        KeyboardAxis1DBinding,
        KeyboardAxis2DBinding,
        MouseDeltaBinding>;

    class InputAction final
    {
    public:
        explicit InputAction(std::string name, InputActionValueType valueType);

        const std::string& GetName() const { return m_Name; }
        InputActionValueType GetValueType() const { return m_ValueType; }

        void AddBinding(InputBinding binding);

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Enabled; }

        // Update based on current device state. Called by InputSystem once per frame.
        void Update(const InputSystem& input);

        // Polling-style access (Unity-like).
        InputActionPhase GetPhase() const { return m_Phase; }
        bool WasStartedThisFrame() const { return m_Phase == InputActionPhase::Started; }
        bool WasPerformedThisFrame() const { return m_Phase == InputActionPhase::Performed; }
        bool WasCanceledThisFrame() const { return m_Phase == InputActionPhase::Canceled; }

        bool IsPressed(float deadzone = 0.0001f) const { return m_Value.IsActuated(deadzone); }
        bool ReadButton() const { return m_Value.AsButton(); }
        float ReadAxis1D() const { return m_Value.AsAxis1D(); }
        glm::vec2 ReadAxis2D() const { return m_Value.AsAxis2D(); }

        // Debug / tooling
        std::string DebugDump() const;

    private:
        InputActionValue EvaluateValue(const InputSystem& input) const;

        std::string m_Name;
        InputActionValueType m_ValueType = InputActionValueType::Button;
        std::vector<InputBinding> m_Bindings;

        bool m_Enabled = true;
        InputActionPhase m_Phase = InputActionPhase::Waiting;
        InputActionValue m_Value{};
        bool m_WasActuatedLastFrame = false;
    };

    class InputActionMap final
    {
    public:
        explicit InputActionMap(std::string name);

        const std::string& GetName() const { return m_Name; }

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return m_Enabled; }

        InputAction& AddAction(const std::string& name, InputActionValueType valueType);
        InputAction* FindAction(std::string_view name);
        const InputAction* FindAction(std::string_view name) const;

        void Update(const InputSystem& input);

    private:
        std::string m_Name;
        bool m_Enabled = true;
        std::vector<std::unique_ptr<InputAction>> m_Actions;
        std::unordered_map<std::string, InputAction*> m_ActionByName;
    };

    class InputActionAsset final
    {
    public:
        InputActionAsset() = default;

        InputActionMap& AddMap(const std::string& name);
        InputActionMap* FindMap(std::string_view name);
        const InputActionMap* FindMap(std::string_view name) const;

        void Update(const InputSystem& input);

    private:
        std::vector<std::unique_ptr<InputActionMap>> m_Maps;
        std::unordered_map<std::string, InputActionMap*> m_MapByName;
    };
}

