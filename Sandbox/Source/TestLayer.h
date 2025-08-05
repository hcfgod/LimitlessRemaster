#pragma once
#include "Limitless.h"
#include <memory>

namespace Limitless
{
    class TestLayer : public Layer
    {
    public:
        TestLayer();
        virtual ~TestLayer() = default;

        // Layer lifecycle
        void OnAttach() override;
        void OnDetach() override;

        // Per-frame updates
        void OnUpdate(float deltaTime) override;
        void OnRender() override;

    private:
        float m_ClearColor[4]; // RGBA color for clearing
        float m_ColorChangeSpeed; // Speed at which to change colors
    };
} 