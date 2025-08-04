#pragma once

#include "EventSystem.h"
#include "Debug/Log.h"
#include <string>
#include <memory>

namespace Limitless
{
    /**
     * @brief Base class for all layers in the application
     * 
     * Layers are organized in a stack where:
     * - Regular layers are added to the bottom of the stack
     * - Overlays are added to the top of the stack
     * - Events are processed from top to bottom (overlays first)
     * - Rendering happens from bottom to top (overlays last)
     */
    class Layer : public EventListener
    {
    public:
        explicit Layer(const std::string& name = "Layer");
        virtual ~Layer() = default;

        // Layer lifecycle
        virtual void OnAttach() {}
        virtual void OnDetach() {}

        // Per-frame updates
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}

        // Event handling (inherited from EventListener)
        void OnEvent(Event& event) override;
        bool ShouldReceiveEvent(const Event& event) const override;
        EventPriority GetPriority() const override { return EventPriority::Normal; }

        // Layer properties
        const std::string& GetName() const { return m_DebugName; }
        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool enabled) { m_Enabled = enabled; }

        // Event filtering - layers can override this to filter specific events
        virtual bool ShouldHandleEvent(const Event& event) const { return true; }

    protected:
        // Override these for specific event handling
        virtual void OnKeyPressed(class Events::KeyPressedEvent& event) {}
        virtual void OnKeyReleased(class Events::KeyReleasedEvent& event) {}
        virtual void OnMouseMoved(class Events::MouseMovedEvent& event) {}
        virtual void OnMouseButtonPressed(class Events::MouseButtonPressedEvent& event) {}
        virtual void OnMouseButtonReleased(class Events::MouseButtonReleasedEvent& event) {}
        virtual void OnMouseScrolled(class Events::MouseScrolledEvent& event) {}
        virtual void OnWindowResize(class Events::WindowResizeEvent& event) {}
        virtual void OnWindowClose(class Events::WindowCloseEvent& event) {}
        
        // Note: Mouse button and scroll events will be added when event classes are implemented

    private:
        std::string m_DebugName;
        bool m_Enabled = true;
    };

    // Smart pointer alias for convenience
    using LayerRef = std::shared_ptr<Layer>;

    /**
     * @brief Creates a new layer and returns a shared pointer to it
     */
    template<typename T, typename... Args>
    LayerRef CreateLayer(Args&&... args)
    {
        static_assert(std::is_base_of_v<Layer, T>, "T must derive from Layer");
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}