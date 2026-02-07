#include "Core/Input/InputAction.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputSystem.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_gamepad.h>
#include <cmath>
#include <sstream>

namespace Limitless
{
    static const char* PhaseToString(InputActionPhase phase)
    {
        switch (phase)
        {
            case InputActionPhase::Disabled: return "Disabled";
            case InputActionPhase::Waiting: return "Waiting";
            case InputActionPhase::Started: return "Started";
            case InputActionPhase::Performed: return "Performed";
            case InputActionPhase::Canceled: return "Canceled";
            default: return "Unknown";
        }
    }

    static const char* ValueTypeToString(InputActionValueType type)
    {
        switch (type)
        {
            case InputActionValueType::Button: return "Button";
            case InputActionValueType::Axis1D: return "Axis1D";
            case InputActionValueType::Axis2D: return "Axis2D";
            default: return "Unknown";
        }
    }

    static std::string ScancodeToString(SDL_Scancode scancode)
    {
        // SDL returns a stable human-readable name for known scancodes.
        const char* name = SDL_GetScancodeName(scancode);
        if (name && name[0] != '\0')
        {
            return std::string(name);
        }
        return std::to_string(static_cast<int>(scancode));
    }

    static std::string GamepadButtonToString(SDL_GamepadButton button)
    {
        const char* s = SDL_GetGamepadStringForButton(button);
        if (s && s[0] != '\0')
        {
            return std::string(s);
        }
        return std::to_string(static_cast<int>(button));
    }

    static std::string GamepadAxisToString(SDL_GamepadAxis axis)
    {
        const char* s = SDL_GetGamepadStringForAxis(axis);
        if (s && s[0] != '\0')
        {
            return std::string(s);
        }
        return std::to_string(static_cast<int>(axis));
    }

    static float ApplyDeadzone1D(float v, float deadzone)
    {
        if (deadzone < 0.0f) { deadzone = 0.0f; }
        if (std::abs(v) <= deadzone)
        {
            return 0.0f;
        }
        const float sign = (v < 0.0f) ? -1.0f : 1.0f;
        const float mag = (std::abs(v) - deadzone) / (1.0f - deadzone);
        return sign * std::clamp(mag, 0.0f, 1.0f);
    }

    static glm::vec2 ApplyDeadzone2D(const glm::vec2& v, float deadzone)
    {
        if (deadzone < 0.0f) { deadzone = 0.0f; }
        const float len = glm::length(v);
        if (len <= deadzone)
        {
            return glm::vec2(0.0f);
        }
        const float scaled = (len - deadzone) / (1.0f - deadzone);
        return glm::normalize(v) * std::clamp(scaled, 0.0f, 1.0f);
    }

    bool InputActionValue::AsButton() const
    {
        if (m_Type == InputActionValueType::Button)
        {
            return std::get<bool>(m_Value);
        }
        if (m_Type == InputActionValueType::Axis1D)
        {
            return std::abs(std::get<float>(m_Value)) > 0.0001f;
        }
        const glm::vec2 v = std::get<glm::vec2>(m_Value);
        return (std::abs(v.x) > 0.0001f) || (std::abs(v.y) > 0.0001f);
    }

    float InputActionValue::AsAxis1D() const
    {
        if (m_Type == InputActionValueType::Axis1D)
        {
            return std::get<float>(m_Value);
        }
        if (m_Type == InputActionValueType::Button)
        {
            return std::get<bool>(m_Value) ? 1.0f : 0.0f;
        }
        // For Axis2D -> Axis1D, return magnitude (simple, useful for debug).
        const glm::vec2 v = std::get<glm::vec2>(m_Value);
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    glm::vec2 InputActionValue::AsAxis2D() const
    {
        if (m_Type == InputActionValueType::Axis2D)
        {
            return std::get<glm::vec2>(m_Value);
        }
        if (m_Type == InputActionValueType::Axis1D)
        {
            return glm::vec2(std::get<float>(m_Value), 0.0f);
        }
        return std::get<bool>(m_Value) ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 0.0f);
    }

    InputActionValue InputActionValue::Button(bool b)
    {
        InputActionValue v;
        v.m_Type = InputActionValueType::Button;
        v.m_Value = b;
        return v;
    }

    InputActionValue InputActionValue::Axis1D(float f)
    {
        InputActionValue v;
        v.m_Type = InputActionValueType::Axis1D;
        v.m_Value = f;
        return v;
    }

    InputActionValue InputActionValue::Axis2D(const glm::vec2& vec)
    {
        InputActionValue v;
        v.m_Type = InputActionValueType::Axis2D;
        v.m_Value = vec;
        return v;
    }

    bool InputActionValue::IsActuated(float deadzone) const
    {
        if (deadzone < 0.0f) { deadzone = 0.0f; }

        switch (m_Type)
        {
            case InputActionValueType::Button:
                return std::get<bool>(m_Value);
            case InputActionValueType::Axis1D:
                return std::abs(std::get<float>(m_Value)) > deadzone;
            case InputActionValueType::Axis2D:
            default:
            {
                const glm::vec2 v = std::get<glm::vec2>(m_Value);
                return (std::abs(v.x) > deadzone) || (std::abs(v.y) > deadzone);
            }
        }
    }

    InputAction::InputAction(std::string name, InputActionValueType valueType)
        : m_Name(std::move(name))
        , m_ValueType(valueType)
    {
        m_Value = InputActionValue::Button(false);
        switch (m_ValueType)
        {
            case InputActionValueType::Button:
                m_Value = InputActionValue::Button(false);
                break;
            case InputActionValueType::Axis1D:
                m_Value = InputActionValue::Axis1D(0.0f);
                break;
            case InputActionValueType::Axis2D:
                m_Value = InputActionValue::Axis2D(glm::vec2(0.0f));
                break;
        }
    }

    void InputAction::AddBinding(InputBinding binding)
    {
        m_Bindings.push_back(std::move(binding));
    }

    bool InputAction::SetBinding(size_t index, InputBinding binding)
    {
        if (index >= m_Bindings.size())
        {
            return false;
        }
        m_Bindings[index] = std::move(binding);
        return true;
    }

    void InputAction::SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
        if (!m_Enabled)
        {
            m_Phase = InputActionPhase::Disabled;
            m_WasActuatedLastFrame = false;
            // Reset value.
            switch (m_ValueType)
            {
                case InputActionValueType::Button:
                    m_Value = InputActionValue::Button(false);
                    break;
                case InputActionValueType::Axis1D:
                    m_Value = InputActionValue::Axis1D(0.0f);
                    break;
                case InputActionValueType::Axis2D:
                default:
                    m_Value = InputActionValue::Axis2D(glm::vec2(0.0f));
                    break;
            }
        }
        else
        {
            m_Phase = InputActionPhase::Waiting;
        }
    }

    void InputAction::Update(const InputSystem& input)
    {
        if (!m_Enabled)
        {
            m_Phase = InputActionPhase::Disabled;
            return;
        }

        const InputActionValue newValue = EvaluateValue(input);
        const bool actuated = newValue.IsActuated();

        // Unity-like phases (simplified):
        // - Started: false -> true
        // - Performed: true (after started) while actuated
        // - Canceled: true -> false
        if (!m_WasActuatedLastFrame && actuated)
        {
            m_Phase = InputActionPhase::Started;
        }
        else if (m_WasActuatedLastFrame && actuated)
        {
            m_Phase = InputActionPhase::Performed;
        }
        else if (m_WasActuatedLastFrame && !actuated)
        {
            m_Phase = InputActionPhase::Canceled;
        }
        else
        {
            m_Phase = InputActionPhase::Waiting;
        }

        m_Value = newValue;
        m_WasActuatedLastFrame = actuated;
    }

    InputActionValue InputAction::EvaluateValue(const InputSystem& input) const
    {
        if (m_Bindings.empty())
        {
            switch (m_ValueType)
            {
                case InputActionValueType::Button: return InputActionValue::Button(false);
                case InputActionValueType::Axis1D: return InputActionValue::Axis1D(0.0f);
                case InputActionValueType::Axis2D: return InputActionValue::Axis2D(glm::vec2(0.0f));
            }
        }

        switch (m_ValueType)
        {
            case InputActionValueType::Button:
            {
                bool down = false;
                for (const auto& b : m_Bindings)
                {
                    if (const auto* key = std::get_if<KeyboardButtonBinding>(&b))
                    {
                        down = down || input.IsKeyDown(key->Key);
                    }
                    else if (const auto* mouse = std::get_if<MouseButtonBinding>(&b))
                    {
                        down = down || input.IsMouseButtonDown(mouse->Button);
                    }
                    else if (const auto* pad = std::get_if<GamepadButtonBinding>(&b))
                    {
                        down = down || input.IsGamepadButtonDown(pad->Button);
                    }
                }
                return InputActionValue::Button(down);
            }
            case InputActionValueType::Axis1D:
            {
                float v = 0.0f;
                for (const auto& b : m_Bindings)
                {
                    if (const auto* axis = std::get_if<KeyboardAxis1DBinding>(&b))
                    {
                        if (input.IsKeyDown(axis->Negative)) { v += axis->NegativeScale; }
                        if (input.IsKeyDown(axis->Positive)) { v += axis->PositiveScale; }
                    }
                    else if (const auto* pad = std::get_if<GamepadAxis1DBinding>(&b))
                    {
                        const float raw = input.GetGamepadAxis(pad->Axis);
                        v += ApplyDeadzone1D(raw, pad->Deadzone) * pad->Scale;
                    }
                }
                // Clamp to [-1, 1] for keyboard axes.
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                return InputActionValue::Axis1D(v);
            }
            case InputActionValueType::Axis2D:
            default:
            {
                glm::vec2 v(0.0f);
                for (const auto& b : m_Bindings)
                {
                    if (const auto* axis = std::get_if<KeyboardAxis2DBinding>(&b))
                    {
                        if (input.IsKeyDown(axis->Left)) { v.x -= axis->Scale; }
                        if (input.IsKeyDown(axis->Right)) { v.x += axis->Scale; }
                        if (input.IsKeyDown(axis->Down)) { v.y -= axis->Scale; }
                        if (input.IsKeyDown(axis->Up)) { v.y += axis->Scale; }
                    }
                    else if (const auto* mouseDelta = std::get_if<MouseDeltaBinding>(&b))
                    {
                        glm::vec2 d = input.GetMouseDelta() * mouseDelta->Sensitivity;
                        if (mouseDelta->InvertY)
                        {
                            d.y = -d.y;
                        }
                        v += d;
                    }
                    else if (const auto* pad = std::get_if<GamepadAxis2DBinding>(&b))
                    {
                        glm::vec2 stick(input.GetGamepadAxis(pad->XAxis), input.GetGamepadAxis(pad->YAxis));
                        if (pad->InvertY)
                        {
                            stick.y = -stick.y;
                        }
                        stick = ApplyDeadzone2D(stick, pad->Deadzone) * pad->Scale;
                        v += stick;
                    }
                }

                return InputActionValue::Axis2D(v);
            }
        }
    }

    std::string InputAction::DebugDump() const
    {
        std::ostringstream ss;
        ss << "InputAction '" << m_Name << "'"
           << " type=" << ValueTypeToString(m_ValueType)
           << " phase=" << PhaseToString(m_Phase)
           << " enabled=" << (m_Enabled ? "true" : "false");

        // Value
        if (m_ValueType == InputActionValueType::Button)
        {
            ss << " value=" << (m_Value.AsButton() ? "true" : "false");
        }
        else if (m_ValueType == InputActionValueType::Axis1D)
        {
            ss << " value=" << m_Value.AsAxis1D();
        }
        else
        {
            const glm::vec2 v = m_Value.AsAxis2D();
            ss << " value=(" << v.x << "," << v.y << ")";
        }

        // Bindings
        ss << " bindings=[";
        for (size_t i = 0; i < m_Bindings.size(); ++i)
        {
            const auto& b = m_Bindings[i];
            if (i > 0) ss << ", ";

            if (const auto* kb = std::get_if<KeyboardButtonBinding>(&b))
            {
                ss << "KeyboardButton(" << ScancodeToString(kb->Key) << ")";
            }
            else if (const auto* mb = std::get_if<MouseButtonBinding>(&b))
            {
                ss << "MouseButton(" << static_cast<int>(mb->Button) << ")";
            }
            else if (const auto* a1 = std::get_if<KeyboardAxis1DBinding>(&b))
            {
                ss << "KeyboardAxis1D(neg=" << ScancodeToString(a1->Negative) << ",pos=" << ScancodeToString(a1->Positive) << ")";
            }
            else if (const auto* a2 = std::get_if<KeyboardAxis2DBinding>(&b))
            {
                ss << "KeyboardAxis2D(U=" << ScancodeToString(a2->Up)
                   << ",D=" << ScancodeToString(a2->Down)
                   << ",L=" << ScancodeToString(a2->Left)
                   << ",R=" << ScancodeToString(a2->Right) << ")";
            }
            else if (const auto* md = std::get_if<MouseDeltaBinding>(&b))
            {
                ss << "MouseDelta(sens=" << md->Sensitivity << ",invertY=" << (md->InvertY ? "true" : "false") << ")";
            }
            else if (const auto* gb = std::get_if<GamepadButtonBinding>(&b))
            {
                ss << "GamepadButton(" << GamepadButtonToString(gb->Button) << ")";
            }
            else if (const auto* g1 = std::get_if<GamepadAxis1DBinding>(&b))
            {
                ss << "GamepadAxis1D(axis=" << GamepadAxisToString(g1->Axis) << ",deadzone=" << g1->Deadzone << ",scale=" << g1->Scale << ")";
            }
            else if (const auto* g2 = std::get_if<GamepadAxis2DBinding>(&b))
            {
                ss << "GamepadAxis2D(x=" << GamepadAxisToString(g2->XAxis)
                   << ",y=" << GamepadAxisToString(g2->YAxis)
                   << ",deadzone=" << g2->Deadzone
                   << ",scale=" << g2->Scale
                   << ",invertY=" << (g2->InvertY ? "true" : "false") << ")";
            }
            else
            {
                ss << "UnknownBinding";
            }
        }
        ss << "]";

        return ss.str();
    }

    InputActionMap::InputActionMap(std::string name)
        : m_Name(std::move(name))
    {
    }

    void InputActionMap::SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
        for (auto& a : m_Actions)
        {
            if (a)
            {
                a->SetEnabled(enabled);
            }
        }
    }

    InputAction& InputActionMap::AddAction(const std::string& name, InputActionValueType valueType)
    {
        if (m_ActionByName.find(name) != m_ActionByName.end())
        {
            LT_CORE_WARN("InputActionMap '{}': action '{}' already exists (returning existing)", m_Name, name);
            return *m_ActionByName[name];
        }

        auto action = std::make_unique<InputAction>(name, valueType);
        InputAction* ptr = action.get();
        m_Actions.push_back(std::move(action));
        m_ActionByName.emplace(name, ptr);
        return *ptr;
    }

    InputAction* InputActionMap::FindAction(std::string_view name)
    {
        const auto it = m_ActionByName.find(std::string(name));
        return (it != m_ActionByName.end()) ? it->second : nullptr;
    }

    const InputAction* InputActionMap::FindAction(std::string_view name) const
    {
        const auto it = m_ActionByName.find(std::string(name));
        return (it != m_ActionByName.end()) ? it->second : nullptr;
    }

    void InputActionMap::Update(const InputSystem& input)
    {
        if (!m_Enabled)
        {
            return;
        }

        for (auto& a : m_Actions)
        {
            if (a)
            {
                a->Update(input);
            }
        }
    }

    InputActionMap& InputActionAsset::AddMap(const std::string& name)
    {
        if (m_MapByName.find(name) != m_MapByName.end())
        {
            LT_CORE_WARN("InputActionAsset: map '{}' already exists (returning existing)", name);
            return *m_MapByName[name];
        }

        auto map = std::make_unique<InputActionMap>(name);
        InputActionMap* ptr = map.get();
        m_Maps.push_back(std::move(map));
        m_MapByName.emplace(name, ptr);
        return *ptr;
    }

    InputActionMap* InputActionAsset::FindMap(std::string_view name)
    {
        const auto it = m_MapByName.find(std::string(name));
        return (it != m_MapByName.end()) ? it->second : nullptr;
    }

    const InputActionMap* InputActionAsset::FindMap(std::string_view name) const
    {
        const auto it = m_MapByName.find(std::string(name));
        return (it != m_MapByName.end()) ? it->second : nullptr;
    }

    void InputActionAsset::Update(const InputSystem& input)
    {
        for (auto& map : m_Maps)
        {
            if (map)
            {
                map->Update(input);
            }
        }
    }
}

