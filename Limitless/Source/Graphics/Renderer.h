#pragma once

#include "GraphicsContext.h"
#include "RenderCommandQueue.h"
#include "Core/Debug/Log.h"
#include <memory>

namespace Limitless
{
    class Renderer
    {
    public:
        static Renderer& GetInstance();
        
        // Initialize the renderer with a graphics context (borrowed, not owned)
        void Initialize(GraphicsContext* context);
        
        // Shutdown the renderer
        void Shutdown();
        
        // Check if renderer is initialized
        bool IsInitialized() const { return m_GraphicsContext != nullptr; }
        
        // Get the graphics context
        GraphicsContext* GetGraphicsContext() const { return m_GraphicsContext; }
        
        // Get the render command queue
        RenderCommandQueue* GetRenderQueue() const { return m_RenderQueue.get(); }
        
        // Submit a render command
        bool SubmitCommand(std::unique_ptr<RenderCommand> command);
        
        // Submit a render command with priority
        bool SubmitCommandWithPriority(std::unique_ptr<RenderCommand> command, RenderCommandPriority priority);
        
        // Execute a command immediately
        void ExecuteImmediate(std::unique_ptr<RenderCommand> command);
        
        // Process all queued commands
        void ProcessCommands();
        
        // Begin a new frame
        void BeginFrame();
        
        // End the current frame
        void EndFrame();
        
        // Swap buffers
        void SwapBuffers();

    private:
        Renderer() = default;
        ~Renderer() = default;
        
        // Disable copy and assignment
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        
        GraphicsContext* m_GraphicsContext = nullptr; // Borrowed, not owned
        std::unique_ptr<RenderCommandQueue> m_RenderQueue;
        bool m_Initialized = false;
    };
} 