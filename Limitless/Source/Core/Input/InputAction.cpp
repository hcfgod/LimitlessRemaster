#include "Core/Input/InputAction.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputSystem.h"

#include <cmath>

namespace Limitless
{
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
                }

                return InputActionValue::Axis2D(v);
            }
        }
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

