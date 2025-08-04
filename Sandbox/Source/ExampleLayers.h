#pragma once

#include "Limitless.h"

namespace Limitless
{
    /**
     * @brief Example background layer that handles basic rendering
     */
    class BackgroundLayer : public Layer
    {
    public:
        BackgroundLayer() : Layer("BackgroundLayer") {}

        void OnAttach() override
        {
            LT_CORE_INFO("BackgroundLayer attached");
        }

        void OnDetach() override
        {
            LT_CORE_INFO("BackgroundLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            // Example update logic - could handle background animations, etc.
            m_Timer += deltaTime;
        }

        void OnRender() override
        {
            // Example rendering - could clear screen, render background, etc.
            LT_CORE_TRACE("BackgroundLayer rendering (timer: {:.2f}s)", m_Timer);
        }

        void OnKeyPressed(Events::KeyPressedEvent& event) override
        {
            LT_CORE_DEBUG("BackgroundLayer received key press: {}", event.GetKeyCode());
            // Background layers typically don't handle input, so we don't mark as handled
        }

    private:
        float m_Timer = 0.0f;
    };

    /**
     * @brief Example game layer that handles main game logic
     */
    class GameLayer : public Layer
    {
    public:
        GameLayer() : Layer("GameLayer") {}

        void OnAttach() override
        {
            LT_CORE_INFO("GameLayer attached");
        }

        void OnDetach() override
        {
            LT_CORE_INFO("GameLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            // Example game logic
            if (m_Active)
            {
                m_GameTime += deltaTime;
            }
        }

        void OnRender() override
        {
            // Example game rendering
            LT_CORE_TRACE("GameLayer rendering (game time: {:.2f}s)", m_GameTime);
        }

        void OnKeyPressed(Events::KeyPressedEvent& event) override
        {
            LT_CORE_DEBUG("GameLayer received key press: {}", event.GetKeyCode());
            
            // Example: Space key to toggle active state
            if (event.GetKeyCode() == 32) // Space key
            {
                m_Active = !m_Active;
                LT_CORE_INFO("GameLayer active state: {}", m_Active);
                event.SetHandled(true); // Mark as handled to prevent other layers from processing
            }
        }

        void OnWindowResize(Events::WindowResizeEvent& event) override
        {
            LT_CORE_INFO("GameLayer handling window resize: {}x{}", event.GetWidth(), event.GetHeight());
            // Handle viewport changes, UI scaling, etc.
        }

    private:
        float m_GameTime = 0.0f;
        bool m_Active = true;
    };

    /**
     * @brief Example debug overlay that shows performance information
     */
    class DebugOverlay : public Layer
    {
    public:
        DebugOverlay() : Layer("DebugOverlay") {}

        void OnAttach() override
        {
            LT_CORE_INFO("DebugOverlay attached");
        }

        void OnDetach() override
        {
            LT_CORE_INFO("DebugOverlay detached");
        }

        void OnUpdate(float deltaTime) override
        {
            // Update performance metrics
            m_FrameTime = deltaTime;
            m_FrameCount++;
            
            m_FpsTimer += deltaTime;
            if (m_FpsTimer >= 1.0f)
            {
                m_Fps = m_FrameCount / m_FpsTimer;
                m_FrameCount = 0;
                m_FpsTimer = 0.0f;
            }
        }

        void OnRender() override
        {
            // Render debug information on top of everything
            if (m_Visible)
            {
                LT_CORE_TRACE("DebugOverlay rendering debug info (FPS: {:.1f}, Frame Time: {:.2f}ms)", 
                              m_Fps, m_FrameTime * 1000.0f);
            }
        }

        void OnKeyPressed(Events::KeyPressedEvent& event) override
        {
            // Example: F1 key to toggle debug overlay
            if (event.GetKeyCode() == 290) // F1 key
            {
                m_Visible = !m_Visible;
                LT_CORE_INFO("DebugOverlay visibility: {}", m_Visible);
                event.SetHandled(true);
            }
        }

        // Override to filter events - we only care about F1 key
        bool ShouldHandleEvent(const Event& event) const override
        {
            if (event.GetType() == EventType::KeyPressed)
            {
                const auto& keyEvent = static_cast<const Events::KeyPressedEvent&>(event);
                return keyEvent.GetKeyCode() == 290; // F1
            }
            return false;
        }

    private:
        float m_FrameTime = 0.0f;
        float m_Fps = 0.0f;
        int m_FrameCount = 0;
        float m_FpsTimer = 0.0f;
        bool m_Visible = true;
    };

    /**
     * @brief Example UI overlay for menus and interface elements
     */
    class UIOverlay : public Layer
    {
    public:
        UIOverlay() : Layer("UIOverlay") {}

        void OnAttach() override
        {
            LT_CORE_INFO("UIOverlay attached");
        }

        void OnDetach() override
        {
            LT_CORE_INFO("UIOverlay detached");
        }

        void OnUpdate(float deltaTime) override
        {
            // Update UI animations, etc.
        }

        void OnRender() override
        {
            // Render UI elements
            if (m_ShowMenu)
            {
                LT_CORE_TRACE("UIOverlay rendering menu");
            }
        }

        void OnKeyPressed(Events::KeyPressedEvent& event) override
        {
            LT_CORE_DEBUG("UIOverlay received key press: {}", event.GetKeyCode());
            
            // Example: Escape key to toggle menu
            if (event.GetKeyCode() == 27) // Escape key
            {
                m_ShowMenu = !m_ShowMenu;
                LT_CORE_INFO("Menu visibility: {}", m_ShowMenu);
                event.SetHandled(true);
            }
        }

        void OnMouseMoved(Events::MouseMovedEvent& event) override
        {
            // Track mouse for UI hover effects
            m_MouseX = event.GetX();
            m_MouseY = event.GetY();
            LT_CORE_TRACE("UIOverlay mouse moved: ({:.1f}, {:.1f})", m_MouseX, m_MouseY);
        }

    private:
        bool m_ShowMenu = false;
        float m_MouseX = 0.0f;
        float m_MouseY = 0.0f;
    };
}