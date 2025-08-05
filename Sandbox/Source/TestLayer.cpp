#include "TestLayer.h"
#include <cmath>

namespace Limitless
{
    TestLayer::TestLayer()
        : Layer("TestLayer")
        , m_ColorChangeSpeed(0.5f) // Change color every 0.5 seconds
    {
        // Initialize clear color to a nice blue
        m_ClearColor[0] = 0.2f; // Red
        m_ClearColor[1] = 0.3f; // Green
        m_ClearColor[2] = 0.8f; // Blue
        m_ClearColor[3] = 1.0f; // Alpha

        LT_INFO("TestLayer created");
    }

    void TestLayer::OnAttach()
    {
        LT_INFO("TestLayer attached");
    }

    void TestLayer::OnDetach()
    {
        LT_INFO("TestLayer detached");
    }

    void TestLayer::OnUpdate(float deltaTime)
    {
        // Update the clear color over time to create a nice visual effect
        static float time = 0.0f;
        time += deltaTime;

        // Create a smooth color transition using sine waves
        m_ClearColor[0] = 0.5f + 0.3f * std::sin(time * m_ColorChangeSpeed);
        m_ClearColor[1] = 0.5f + 0.3f * std::sin(time * m_ColorChangeSpeed + 2.0f);
        m_ClearColor[2] = 0.5f + 0.3f * std::sin(time * m_ColorChangeSpeed + 4.0f);
        m_ClearColor[3] = 1.0f; // Keep alpha at 1.0

        // Clamp values to valid range
        for (int i = 0; i < 3; ++i)
        {
            m_ClearColor[i] = std::max(0.0f, std::min(1.0f, m_ClearColor[i]));
        }
    }

    void TestLayer::OnRender()
    {
        auto& renderer = Renderer::GetInstance();
        
        if (!renderer.IsInitialized())
        {
            LT_WARN("Renderer not initialized in TestLayer");
            return;
        }

        // Create a clear command with our current color
        ClearCommand::ClearFlags flags;
        flags.color = true;
        flags.depth = true;
        flags.stencil = false;

        auto clearCommand = std::make_unique<ClearCommand>(
            flags,
            m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]
        );

        // Submit the clear command to the global renderer
        if (!renderer.SubmitCommand(std::move(clearCommand)))
        {
            LT_WARN("Failed to submit clear command to renderer");
        }
    }
} 