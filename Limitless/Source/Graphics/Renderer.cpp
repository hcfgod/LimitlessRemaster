#include "Renderer.h"
#include "Core/Debug/Log.h"

namespace Limitless
{
    Renderer& Renderer::GetInstance()
    {
        static Renderer instance;
        return instance;
    }

    void Renderer::Initialize(GraphicsContext* context)
    {
        if (m_Initialized)
        {
            LT_CORE_WARN("Renderer already initialized");
            return;
        }

        if (!context)
        {
            LT_CORE_ERROR("Cannot initialize renderer with null graphics context");
            return;
        }

        m_GraphicsContext = context;
        
        // Create render command queue with default configuration
        RenderQueueConfig config;
        config.maxQueueSize = 16384;
        config.maxCommandsPerFrame = 10000;
        config.enableBatching = true;
        config.enablePrioritySorting = true;
        config.enableStatistics = true;
        
        m_RenderQueue = std::make_unique<RenderCommandQueue>(config);
        
        m_Initialized = true;
        LT_CORE_INFO("Renderer initialized successfully");
    }

    void Renderer::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        // Process any remaining commands
        if (m_RenderQueue)
        {
            m_RenderQueue->Flush();
        }

        m_RenderQueue.reset();
        m_GraphicsContext = nullptr; // Don't delete, we don't own it
        m_Initialized = false;
        
        LT_CORE_INFO("Renderer shutdown successfully");
    }

    bool Renderer::SubmitCommand(std::unique_ptr<RenderCommand> command)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot submit command - renderer not initialized");
            return false;
        }

        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command to renderer");
            return false;
        }

        return m_RenderQueue->SubmitCommand(std::move(command));
    }

    bool Renderer::SubmitCommandWithPriority(std::unique_ptr<RenderCommand> command, RenderCommandPriority priority)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot submit priority command - renderer not initialized");
            return false;
        }

        return m_RenderQueue->SubmitCommandWithPriority(std::move(command), priority);
    }

    void Renderer::ExecuteImmediate(std::unique_ptr<RenderCommand> command)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot execute command immediately - renderer not initialized");
            return;
        }

        m_RenderQueue->ExecuteImmediate(std::move(command));
    }

    void Renderer::ProcessCommands()
    {
        if (!m_Initialized || !m_RenderQueue || !m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot process commands - renderer not initialized");
            return;
        }

        m_RenderQueue->ProcessCommands(m_GraphicsContext);
    }

    void Renderer::BeginFrame()
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot begin frame - renderer not initialized");
            return;
        }

        m_RenderQueue->BeginFrame();
    }

    void Renderer::EndFrame()
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot end frame - renderer not initialized");
            return;
        }

        // Process any remaining commands for this frame
        ProcessCommands();
    }

    void Renderer::SwapBuffers()
    {
        if (!m_Initialized || !m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot swap buffers - renderer not initialized");
            return;
        }

        m_GraphicsContext->SwapBuffers();
    }
} 