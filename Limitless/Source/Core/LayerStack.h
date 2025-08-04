#pragma once

#include "Layer.h"
#include "EventSystem.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace Limitless
{
    /**
     * @brief Manages a stack of layers with overlay support
     * 
     * The LayerStack manages two sections:
     * 1. Regular layers - added to the bottom part of the stack
     * 2. Overlays - added to the top part of the stack
     * 
     * Event Processing Order: Top to Bottom (Overlays → Regular Layers)
     * Rendering Order: Bottom to Top (Regular Layers → Overlays)
     * 
     * This ensures overlays receive events first and render on top.
     */
    class LayerStack : public EventListener
    {
    public:
        LayerStack();
        ~LayerStack();

        // Layer management
        void PushLayer(LayerRef layer);
        void PushOverlay(LayerRef overlay);
        void PopLayer(LayerRef layer);
        void PopOverlay(LayerRef overlay);

        // Remove layer by pointer or name
        bool RemoveLayer(LayerRef layer);
        bool RemoveLayer(const std::string& name);

        // Layer access
        LayerRef GetLayer(const std::string& name) const;
        bool HasLayer(const std::string& name) const;
        size_t GetLayerCount() const { return m_Layers.size(); }
        size_t GetOverlayCount() const { return m_Layers.size() - m_LayerInsertIndex; }

        // Layer stack operations
        void Clear();
        void SetLayerEnabled(const std::string& name, bool enabled);

        // Per-frame updates
        void OnUpdate(float deltaTime);
        void OnRender();

        // Event handling (EventListener interface)
        void OnEvent(Event& event) override;
        bool ShouldReceiveEvent(const Event& event) const override { return true; }
        EventPriority GetPriority() const override { return EventPriority::High; }

        // Iteration support (for debugging/inspection)
        auto begin() const { return m_Layers.begin(); }
        auto end() const { return m_Layers.end(); }
        auto rbegin() const { return m_Layers.rbegin(); }
        auto rend() const { return m_Layers.rend(); }

        // Statistics
        struct LayerStackStats
        {
            size_t totalLayers;
            size_t regularLayers;
            size_t overlays;
            size_t enabledLayers;
            size_t disabledLayers;
        };
        LayerStackStats GetStats() const;

    private:
        std::vector<LayerRef> m_Layers;
        size_t m_LayerInsertIndex = 0; // Separates regular layers from overlays

        // Helper methods
        void AttachLayer(LayerRef layer);
        void DetachLayer(LayerRef layer);
        std::vector<LayerRef>::const_iterator FindLayer(LayerRef layer) const;
        std::vector<LayerRef>::const_iterator FindLayer(const std::string& name) const;
    };
}