#pragma once

#include "Core/Input/InputAction.h"

#include <glm/glm.hpp>

#include <SDL3/SDL_events.h>

#include <array>
#include <cstdint>
#include <memory>

namespace Limitless
{
    class InputSystem final
    {
    public:
        static InputSystem& GetInstance();

        InputSystem() = default;
        ~InputSystem() = default;

        InputSystem(const InputSystem&) = delete;
        InputSystem& operator=(const InputSystem&) = delete;

        void BeginFrame();

        // Feed raw SDL events.
        void OnSdlEvent(const SDL_Event& event);

        // Evaluate enabled action maps (should be called once per frame after events are pumped).
        void UpdateActions();

        void SetActionAsset(std::shared_ptr<InputActionAsset> asset) { m_ActionAsset = std::move(asset); }
        std::shared_ptr<InputActionAsset> GetActionAsset() const { return m_ActionAsset; }

        // Device state polling
        bool IsKeyDown(SDL_Scancode scancode) const;
        bool WasKeyPressedThisFrame(SDL_Scancode scancode) const;
        bool WasKeyReleasedThisFrame(SDL_Scancode scancode) const;

        bool IsMouseButtonDown(uint8_t button) const;
        bool WasMouseButtonPressedThisFrame(uint8_t button) const;
        bool WasMouseButtonReleasedThisFrame(uint8_t button) const;

        glm::vec2 GetMousePosition() const { return m_MousePosition; }
        glm::vec2 GetMouseDelta() const { return m_MouseDelta; }
        glm::vec2 GetMouseWheelDelta() const { return m_MouseWheelDelta; }

    private:
        static constexpr size_t kMaxMouseButtons = 8;

        void OnKey(SDL_Scancode scancode, bool down, bool repeat);
        void OnMouseMotion(float x, float y, float dx, float dy);
        void OnMouseButton(uint8_t button, bool down);
        void OnMouseWheel(float x, float y);

        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyDown{};
        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyPressedThisFrame{};
        std::array<uint8_t, SDL_SCANCODE_COUNT> m_KeyReleasedThisFrame{};

        std::array<uint8_t, kMaxMouseButtons> m_MouseDown{};
        std::array<uint8_t, kMaxMouseButtons> m_MousePressedThisFrame{};
        std::array<uint8_t, kMaxMouseButtons> m_MouseReleasedThisFrame{};

        glm::vec2 m_MousePosition{0.0f, 0.0f};
        glm::vec2 m_MouseDelta{0.0f, 0.0f};
        glm::vec2 m_MouseWheelDelta{0.0f, 0.0f};

        std::shared_ptr<InputActionAsset> m_ActionAsset;
    };

    inline InputSystem& GetInputSystem() { return InputSystem::GetInstance(); }
}

