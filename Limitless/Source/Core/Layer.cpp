#include "Layer.h"
#include "EventSystem.h"

namespace Limitless
{
    Layer::Layer(const std::string& name)
        : m_DebugName(name)
    {
        LT_CORE_DEBUG("Layer '{}' created", m_DebugName);
    }

    void Layer::OnEvent(Event& event)
    {
        if (!m_Enabled || !ShouldHandleEvent(event))
            return;

        // Dispatch to specific event handlers based on event type
        switch (event.GetType())
        {
        case EventType::KeyPressed:
            OnKeyPressed(static_cast<Events::KeyPressedEvent&>(event));
            break;
        case EventType::KeyReleased:
            OnKeyReleased(static_cast<Events::KeyReleasedEvent&>(event));
            break;
        case EventType::MouseMoved:
            OnMouseMoved(static_cast<Events::MouseMovedEvent&>(event));
            break;
        case EventType::MouseButtonPressed:
            OnMouseButtonPressed(static_cast<Events::MouseButtonPressedEvent&>(event));
            break;
        case EventType::MouseButtonReleased:
            OnMouseButtonReleased(static_cast<Events::MouseButtonReleasedEvent&>(event));
            break;
        case EventType::MouseScrolled:
            OnMouseScrolled(static_cast<Events::MouseScrolledEvent&>(event));
            break;
        case EventType::WindowResize:
            OnWindowResize(static_cast<Events::WindowResizeEvent&>(event));
            break;
        case EventType::WindowClose:
            OnWindowClose(static_cast<Events::WindowCloseEvent&>(event));
            break;
        default:
            // For custom events or events not explicitly handled
            break;
        }
    }

    bool Layer::ShouldReceiveEvent(const Event& event) const
    {
        return m_Enabled && ShouldHandleEvent(event);
    }
}