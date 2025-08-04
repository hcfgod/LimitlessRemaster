#include "LayerStack.h"
#include "Debug/Log.h"

namespace Limitless
{
    LayerStack::LayerStack()
    {
        LT_CORE_DEBUG("LayerStack created");
    }

    LayerStack::~LayerStack()
    {
        Clear();
        LT_CORE_DEBUG("LayerStack destroyed");
    }

    void LayerStack::PushLayer(LayerRef layer)
    {
        if (!layer)
        {
            LT_CORE_WARN("Attempted to push null layer");
            return;
        }

        // Insert at the end of regular layers (before overlays)
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
        m_LayerInsertIndex++;
        
        AttachLayer(layer);
        LT_CORE_DEBUG("Pushed layer '{}' at position {}", layer->GetName(), m_LayerInsertIndex - 1);
    }

    void LayerStack::PushOverlay(LayerRef overlay)
    {
        if (!overlay)
        {
            LT_CORE_WARN("Attempted to push null overlay");
            return;
        }

        // Insert at the end (top of stack)
        m_Layers.emplace_back(overlay);
        
        AttachLayer(overlay);
        LT_CORE_DEBUG("Pushed overlay '{}' at position {}", overlay->GetName(), m_Layers.size() - 1);
    }

    void LayerStack::PopLayer(LayerRef layer)
    {
        if (!RemoveLayer(layer))
        {
            LT_CORE_WARN("Attempted to pop layer '{}' that doesn't exist", 
                         layer ? layer->GetName() : "null");
        }
    }

    void LayerStack::PopOverlay(LayerRef overlay)
    {
        if (!RemoveLayer(overlay))
        {
            LT_CORE_WARN("Attempted to pop overlay '{}' that doesn't exist", 
                         overlay ? overlay->GetName() : "null");
        }
    }

    bool LayerStack::RemoveLayer(LayerRef layer)
    {
        if (!layer)
            return false;

        auto it = FindLayer(layer);
        if (it != m_Layers.end())
        {
            DetachLayer(*it);
            
            // Adjust insert index if we're removing a regular layer
            if (it < m_Layers.begin() + m_LayerInsertIndex)
            {
                m_LayerInsertIndex--;
            }
            
            m_Layers.erase(it);
            LT_CORE_DEBUG("Removed layer '{}'", layer->GetName());
            return true;
        }
        
        return false;
    }

    bool LayerStack::RemoveLayer(const std::string& name)
    {
        auto it = FindLayer(name);
        if (it != m_Layers.end())
        {
            LayerRef layer = *it;
            DetachLayer(layer);
            
            // Adjust insert index if we're removing a regular layer
            if (it < m_Layers.begin() + m_LayerInsertIndex)
            {
                m_LayerInsertIndex--;
            }
            
            m_Layers.erase(it);
            LT_CORE_DEBUG("Removed layer '{}'", name);
            return true;
        }
        
        LT_CORE_WARN("Layer '{}' not found for removal", name);
        return false;
    }

    LayerRef LayerStack::GetLayer(const std::string& name) const
    {
        auto it = FindLayer(name);
        return (it != m_Layers.end()) ? *it : nullptr;
    }

    bool LayerStack::HasLayer(const std::string& name) const
    {
        return FindLayer(name) != m_Layers.end();
    }

    void LayerStack::Clear()
    {
        // Detach all layers in reverse order
        for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
        {
            DetachLayer(*it);
        }
        
        m_Layers.clear();
        m_LayerInsertIndex = 0;
        LT_CORE_DEBUG("LayerStack cleared");
    }

    void LayerStack::SetLayerEnabled(const std::string& name, bool enabled)
    {
        LayerRef layer = GetLayer(name);
        if (layer)
        {
            layer->SetEnabled(enabled);
            LT_CORE_DEBUG("Layer '{}' enabled state set to {}", name, enabled);
        }
        else
        {
            LT_CORE_WARN("Cannot set enabled state for layer '{}' - not found", name);
        }
    }

    void LayerStack::OnUpdate(float deltaTime)
    {
        // Update layers from bottom to top (regular layers first, then overlays)
        for (auto& layer : m_Layers)
        {
            if (layer && layer->IsEnabled())
            {
                layer->OnUpdate(deltaTime);
            }
        }
    }

    void LayerStack::OnRender()
    {
        // Render layers from bottom to top (regular layers first, then overlays)
        for (auto& layer : m_Layers)
        {
            if (layer && layer->IsEnabled())
            {
                layer->OnRender();
            }
        }
    }

    void LayerStack::OnEvent(Event& event)
    {
        // Process events from top to bottom (overlays first, then regular layers)
        // This ensures overlays can handle events before underlying layers
        for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
        {
            auto& layer = *it;
            if (layer && layer->IsEnabled() && layer->ShouldReceiveEvent(event))
            {
                layer->OnEvent(event);
                
                // If the event was handled, we can stop propagating
                if (event.IsHandled())
                {
                    LT_CORE_DEBUG("Event '{}' handled by layer '{}'", 
                                  event.GetName(), layer->GetName());
                    break;
                }
            }
        }
    }

    LayerStack::LayerStackStats LayerStack::GetStats() const
    {
        LayerStackStats stats{};
        stats.totalLayers = m_Layers.size();
        stats.regularLayers = m_LayerInsertIndex;
        stats.overlays = m_Layers.size() - m_LayerInsertIndex;
        
        for (const auto& layer : m_Layers)
        {
            if (layer)
            {
                if (layer->IsEnabled())
                    stats.enabledLayers++;
                else
                    stats.disabledLayers++;
            }
        }
        
        return stats;
    }

    // Private helper methods
    void LayerStack::AttachLayer(LayerRef layer)
    {
        if (layer)
        {
            layer->OnAttach();
            
            // Register layer with event system
            auto& eventSystem = GetEventSystem();
            eventSystem.AddListener(layer);
            
            LT_CORE_DEBUG("Layer '{}' attached and registered with event system", layer->GetName());
        }
    }

    void LayerStack::DetachLayer(LayerRef layer)
    {
        if (layer)
        {
            // Unregister from event system
            auto& eventSystem = GetEventSystem();
            eventSystem.RemoveListener(layer);
            
            layer->OnDetach();
            LT_CORE_DEBUG("Layer '{}' detached and unregistered from event system", layer->GetName());
        }
    }

    std::vector<LayerRef>::const_iterator LayerStack::FindLayer(LayerRef layer) const
    {
        return std::find(m_Layers.begin(), m_Layers.end(), layer);
    }

    std::vector<LayerRef>::const_iterator LayerStack::FindLayer(const std::string& name) const
    {
        return std::find_if(m_Layers.begin(), m_Layers.end(),
            [&name](const LayerRef& layer) {
                return layer && layer->GetName() == name;
            });
    }
}