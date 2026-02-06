#include "Renderer.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include "Graphics/OpenGL/OpenGLContext.h"

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

        // Default behavior: enable render thread for OpenGL unless explicitly disabled.
        // This allows OpenGL work to be executed on a dedicated thread (context-affine) while
        // still supporting multi-threaded submission.
        const bool enableRenderThread = ConfigManager::GetInstance().GetValue<bool>("graphics.render_thread_enabled", true);
        EnableRenderThread(enableRenderThread);
    }

    void Renderer::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        // Drain any pending resource work while the context is still valid.
        // This ensures deletes/uploads complete before the render thread exits.
        if (m_RenderThreadEnabled.load(std::memory_order_relaxed) && m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            try
            {
                SubmitResourceAndWait([](GraphicsContext*) { /* barrier */ });
            }
            catch (const std::exception& e)
            {
                LT_CORE_WARN("Renderer shutdown resource barrier failed: {}", e.what());
            }
        }

        StopRenderThread();

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

    bool Renderer::SubmitResource(std::unique_ptr<RenderResourceCommandQueue::Command> command)
    {
        if (!command)
        {
            return false;
        }

        if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            LT_CORE_WARN("Renderer::SubmitResource called while render thread is not running (dropping)");
            return false;
        }

        if (!m_ResourceQueue.Submit(std::move(command)))
        {
            return false;
        }

        NotifyRenderThreadResourceWorkAvailable();
        return true;
    }

    void Renderer::ExecuteImmediate(std::unique_ptr<RenderCommand> command)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot execute command immediately - renderer not initialized");
            return;
        }

        if (!m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot execute command immediately - graphics context is null");
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(m_GraphicsContext))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
            m_RenderQueue->ExecuteImmediate(m_GraphicsContext, std::move(command));
            return;
        }

        m_GraphicsContext->MakeCurrent();
        m_RenderQueue->ExecuteImmediate(m_GraphicsContext, std::move(command));
    }

    void Renderer::ProcessCommands()
    {
        if (!m_Initialized || !m_RenderQueue || !m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot process commands - renderer not initialized");
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(m_GraphicsContext))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
            m_RenderQueue->ProcessCommands(m_GraphicsContext);
            return;
        }

        m_GraphicsContext->MakeCurrent();
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

        if (m_RenderThreadEnabled.load(std::memory_order_relaxed) && m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            // Signal the render thread that a new frame is ready to execute/present.
            std::lock_guard<std::mutex> lock(m_RenderThreadMutex);
            m_FrameRequested = true;
            m_FrameRequestedId++;
            m_RenderThreadCV.notify_one();
            return;
        }

        // Single-thread fallback: process any remaining commands for this frame on the calling thread.
        ProcessCommands();
    }

    void Renderer::SwapBuffers()
    {
        if (!m_Initialized || !m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot swap buffers - renderer not initialized");
            return;
        }

        if (m_RenderThreadEnabled.load(std::memory_order_relaxed) && m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            // Wait until the render thread completes the most recently requested frame.
            std::unique_lock<std::mutex> lock(m_RenderThreadMutex);
            const uint64_t targetFrameId = m_FrameRequestedId;
            m_RenderThreadCV.wait(lock, [this, targetFrameId]() {
                return m_RenderThreadShutdown.load() || m_FrameCompletedId >= targetFrameId;
            });
            return;
        }

        if (auto* glContext = dynamic_cast<OpenGLContext*>(m_GraphicsContext))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
            m_GraphicsContext->SwapBuffers();
            return;
        }

        m_GraphicsContext->MakeCurrent();
        m_GraphicsContext->SwapBuffers();
    }

    void Renderer::EnableRenderThread(bool enable)
    {
        if (enable)
        {
            m_RenderThreadEnabled.store(true, std::memory_order_relaxed);
            StartRenderThread();
        }
        else
        {
            m_RenderThreadEnabled.store(false, std::memory_order_relaxed);
            StopRenderThread();
        }
    }

    void Renderer::StartRenderThread()
    {
        if (!m_Initialized || !m_GraphicsContext || !m_RenderQueue)
        {
            return;
        }

        if (m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            return;
        }

        // Only enable for OpenGL today (context affinity model).
        if (dynamic_cast<OpenGLContext*>(m_GraphicsContext) == nullptr)
        {
            LT_CORE_WARN("Renderer: render thread enabled, but graphics context is not OpenGL; disabling render thread");
            m_RenderThreadEnabled.store(false, std::memory_order_relaxed);
            return;
        }

        m_RenderThreadShutdown.store(false, std::memory_order_relaxed);
        m_RenderThreadRunning.store(true, std::memory_order_relaxed);
        m_RenderThread = std::thread(&Renderer::RenderThreadMain, this);
        LT_CORE_INFO("Renderer: render thread started");
    }

    void Renderer::StopRenderThread()
    {
        if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            return;
        }

        m_RenderThreadShutdown.store(true, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(m_RenderThreadMutex);
            m_FrameRequested = true; // wake the render thread if it's waiting
            m_RenderThreadCV.notify_all();
        }

        if (m_RenderThread.joinable())
        {
            m_RenderThread.join();
        }

        m_RenderThreadRunning.store(false, std::memory_order_relaxed);
        LT_CORE_INFO("Renderer: render thread stopped");
    }

    void Renderer::NotifyRenderThreadResourceWorkAvailable()
    {
        if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            return;
        }

        // Wake the render thread even if there is no pending frame request yet.
        std::lock_guard<std::mutex> lock(m_RenderThreadMutex);
        m_RenderThreadCV.notify_one();
    }

    void Renderer::RenderThreadMain()
    {
        auto* glContext = dynamic_cast<OpenGLContext*>(m_GraphicsContext);
        if (!glContext)
        {
            return;
        }

        m_RenderThreadId = std::this_thread::get_id();

        while (!m_RenderThreadShutdown.load(std::memory_order_relaxed))
        {
            uint64_t frameIdToComplete = 0;

            // Wait for frame request.
            {
                std::unique_lock<std::mutex> lock(m_RenderThreadMutex);
                m_RenderThreadCV.wait(lock, [this]() {
                    return m_FrameRequested || m_RenderThreadShutdown.load() || !m_ResourceQueue.IsEmpty();
                });
                if (m_RenderThreadShutdown.load(std::memory_order_relaxed))
                {
                    break;
                }

                // Always process any pending resource work before frame execution.
                // (We keep the lock only for the wait/flags; resource processing happens outside.)

                // Consume the request.
                if (m_FrameRequested)
                {
                    m_FrameRequested = false;
                    frameIdToComplete = m_FrameRequestedId;
                }
            }

            // Execute resource commands + frame commands under a context-current scope.
            try
            {
                OpenGLContext::ScopedCurrentContext scope(*glContext);

                // Drain resource work first.
                m_ResourceQueue.Process(m_GraphicsContext, 2048);

                if (frameIdToComplete != 0)
                {
                m_RenderQueue->ProcessCommands(m_GraphicsContext);
                m_GraphicsContext->SwapBuffers();
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Renderer render thread exception: {}", e.what());
            }

            // Mark completion and notify waiters (`SwapBuffers()`).
            {
                std::lock_guard<std::mutex> lock(m_RenderThreadMutex);
                if (frameIdToComplete != 0 && frameIdToComplete > m_FrameCompletedId)
                {
                    m_FrameCompletedId = frameIdToComplete;
                }
            }

            m_RenderThreadCV.notify_all();
        }

        m_RenderThreadId = std::thread::id{};
    }
} 