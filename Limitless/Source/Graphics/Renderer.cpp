#include "Renderer.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLContext.h"
#include "Graphics/OpenGL/OpenGLGPUMetrics.h"
#include "Graphics/OpenGL/OpenGLSharedContext.h"

#if __has_include(<glad/glad.h>)
    #include <glad/glad.h>
#endif

namespace Limitless
{
    void Renderer::SynchronizeOpenGLResourceWorkForCrossContextVisibility()
    {
#if __has_include(<glad/glad.h>)
        // Create a GPU fence and wait for completion on the CPU.
        // This is intentionally conservative: it provides a clear "safe-to-use" boundary when
        // sharing resources across multiple OpenGL contexts.
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!fence)
        {
            LT_CORE_WARN("Renderer: glFenceSync failed; falling back to glFinish");
            glFinish();
            return;
        }

        glFlush();

        for (;;)
        {
            const GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000ull);
            if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
            {
                break;
            }
            if (result == GL_WAIT_FAILED)
            {
                LT_CORE_WARN("Renderer: glClientWaitSync failed; falling back to glFinish");
                glFinish();
                break;
            }
        }

        glDeleteSync(fence);
#else
        // If GL sync APIs are not available in this build, we can't safely provide cross-context
        // visibility semantics. This build should be configured with GLAD or equivalent.
        LT_CORE_WARN("Renderer: OpenGL sync APIs not available; cross-context resource visibility may be unsafe");
#endif
    }

    Renderer& Renderer::GetInstance()
    {
        static Renderer instance;
        return instance;
    }

    GraphicsAPI Renderer::GetActiveAPI() const
    {
        if (m_GraphicsContext)
            return m_GraphicsContext->GetAPI();

        return GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
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

        // Upload staging allocator (frame-local). This is purely CPU memory.
        m_FrameUploadAllocator.Initialize();
        m_FrameCommandArena.Initialize();

        // Default behavior: enable render thread for OpenGL unless explicitly disabled.
        // This allows OpenGL work to be executed on a dedicated thread (context-affine) while
        // still supporting multi-threaded submission.
        const bool enableRenderThread = ConfigManager::GetInstance().GetValue<bool>("graphics.render_thread_enabled", true);

        // Optional OpenGL shared-context resource thread (GPU resource execution in parallel).
        // We create the shared context on the init thread before the render thread takes ownership
        // of the primary context.
        if (enableRenderThread)
        {
            const bool enableResourceThread = ConfigManager::GetInstance().GetValue<bool>("graphics.opengl.resource_thread_enabled", true);
            const bool enableSharedContext = ConfigManager::GetInstance().GetValue<bool>("graphics.opengl.shared_context_enabled", true);

            if (enableResourceThread && enableSharedContext)
            {
                if (auto* glContext = dynamic_cast<OpenGLContext*>(m_GraphicsContext))
                {
                    std::unique_ptr<OpenGLSharedContext> shared = glContext->CreateSharedContext();
                    if (shared)
                    {
                        try
                        {
                            m_OpenGLResourceThread = std::make_unique<RenderResourceThread>(m_ResourceQueue, m_GraphicsContext, std::move(shared));
                            m_OpenGLResourceThread->Start();
                            m_OpenGLResourceThreadEnabled.store(true, std::memory_order_relaxed);
                        }
                        catch (const std::exception& e)
                        {
                            LT_CORE_WARN("Renderer: failed to start OpenGL resource thread: {}", e.what());
                            m_OpenGLResourceThread.reset();
                            m_OpenGLResourceThreadEnabled.store(false, std::memory_order_relaxed);
                        }
                    }
                    else
                    {
                        LT_CORE_WARN("Renderer: shared OpenGL context creation failed; resource thread disabled");
                    }
                }
            }
        }

        EnableRenderThread(enableRenderThread);
    }

    void Renderer::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        // Drain any pending GPU work while the context is still valid.
        // This ensures deletes/uploads and queued render commands complete before the render thread exits.
        if (m_RenderThreadEnabled.load(std::memory_order_relaxed) && m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            try
            {
                // Run the drain on the render thread (context-affine).
                SubmitResourceAndWait("Renderer/Shutdown/DrainRenderCommands", [this](GraphicsContext* context) {
                    // Drain render commands until the queue is empty. ProcessCommands has a per-call
                    // cap (maxCommandsPerFrame), so loop to guarantee we fully drain during shutdown.
                    while (m_RenderQueue && m_RenderQueue->GetSize() != 0)
                    {
                        m_RenderQueue->ProcessCommands(context);
                    }
                });
            }
            catch (const std::exception& e)
            {
                LT_CORE_WARN("Renderer shutdown GPU drain failed: {}", e.what());
            }
        }
        else
        {
            // Single-thread fallback: process queued commands directly on the calling thread.
            // (Requires a valid context; ProcessCommands performs its own checks.)
            ProcessCommands();
        }

        // At this point, the render command queue should be idle. Stopping the render thread after
        // draining avoids the "Flush() drops commands because there is no context" failure mode.
        StopRenderThread();

        if (m_OpenGLResourceThread)
        {
            m_OpenGLResourceThread->Stop();
            m_OpenGLResourceThread.reset();
        }
        m_OpenGLResourceThreadEnabled.store(false, std::memory_order_relaxed);

        m_RenderQueue.reset();
        m_FrameUploadAllocator.Shutdown();
        m_FrameCommandArena.Shutdown();
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

        RenderCommand* raw = command.release();
        return SubmitCommand(UniqueRenderCommand(raw, RenderCommandDeleter{ [](RenderCommand* c) { delete c; } }));
    }

    bool Renderer::SubmitCommand(UniqueRenderCommand command)
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

        RenderCommand* raw = command.release();
        return SubmitCommandWithPriority(UniqueRenderCommand(raw, RenderCommandDeleter{ [](RenderCommand* c) { delete c; } }), priority);
    }

    bool Renderer::SubmitCommandWithPriority(UniqueRenderCommand command, RenderCommandPriority priority)
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

        NotifyResourceWorkAvailable();
        return true;
    }

    void Renderer::ExecuteImmediate(std::unique_ptr<RenderCommand> command)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot execute command immediately - renderer not initialized");
            return;
        }

        if (!command)
        {
            LT_CORE_WARN("Attempted to execute null command immediately");
            return;
        }

        if (!m_GraphicsContext)
        {
            LT_CORE_WARN("Cannot execute command immediately - graphics context is null");
            return;
        }

        RenderCommand* raw = command.release();
        ExecuteImmediate(UniqueRenderCommand(raw, RenderCommandDeleter{ [](RenderCommand* c) { delete c; } }));
    }

    void Renderer::ExecuteImmediate(UniqueRenderCommand command)
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot execute command immediately - renderer not initialized");
            return;
        }

        if (!command)
        {
            LT_CORE_WARN("Attempted to execute null command immediately");
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
            UpdateGPUMetricsFromOpenGL();
            return;
        }

        m_GraphicsContext->MakeCurrent();
        m_RenderQueue->ProcessCommands(m_GraphicsContext);
        UpdateGPUMetricsFromOpenGL();
    }

    void Renderer::BeginFrame()
    {
        if (!m_Initialized || !m_RenderQueue)
        {
            LT_CORE_WARN("Cannot begin frame - renderer not initialized");
            return;
        }

        // Reset frame-local upload allocator. This must happen on the submission thread,
        // and the allocator must not reuse memory still referenced by in-flight commands.
        // The current engine frame model blocks in SwapBuffers(), so there is only one
        // frame in flight; the allocator remains triple-buffered defensively.
        m_FrameUploadFrameId++;
        m_FrameUploadAllocator.BeginFrame(m_FrameUploadFrameId);
        m_FrameCommandArena.BeginFrame(m_FrameUploadFrameId);

        m_RenderQueue->BeginFrame();
    }

    void* Renderer::AllocateFrameUpload(size_t sizeBytes, size_t alignment)
    {
        return m_FrameUploadAllocator.Allocate(sizeBytes, alignment);
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
        GraphicsContext* graphicsContext = m_GraphicsContext;
        if (!m_Initialized || !graphicsContext)
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

        if (auto* glContext = dynamic_cast<OpenGLContext*>(graphicsContext))
        {
            OpenGLContext::ScopedCurrentContext scope(*glContext);
            graphicsContext->SwapBuffers();
            return;
        }

        graphicsContext->MakeCurrent();
        graphicsContext->SwapBuffers();
    }

    void Renderer::SetViewport(int x, int y, int width, int height)
    {
        GraphicsContext* graphicsContext = m_GraphicsContext;
        if (!m_Initialized || !graphicsContext)
        {
            LT_CORE_WARN("Cannot set viewport - renderer not initialized");
            return;
        }

        if (width <= 0 || height <= 0)
        {
            return;
        }

        if (m_RenderThreadEnabled.load(std::memory_order_relaxed) && m_RenderThreadRunning.load(std::memory_order_relaxed))
        {
            SubmitPrimaryResourceAndWait("Renderer/SetViewport", [x, y, width, height](GraphicsContext* context) {
                if (context)
                {
                    context->SetViewport(x, y, width, height);
                }
            });
            return;
        }

        graphicsContext->SetViewport(x, y, width, height);
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

    void Renderer::NotifyResourceWorkAvailable()
    {
        // Always wake the render thread as a safety net.
        //
        // IMPORTANT:
        // On some platforms/driver stacks, the shared-context resource thread can stall inside
        // SDL_GL_MakeCurrent (WGL contention, window message pump interactions, etc).
        // Many engine startup paths use SubmitResourceAndWait(), so if the render thread is not
        // also woken it can sleep indefinitely and deadlock the engine before the first frame.
        NotifyRenderThreadResourceWorkAvailable();

        // Prefer resource thread when enabled; otherwise the render thread drains resource work.
        if (m_OpenGLResourceThread && m_OpenGLResourceThreadEnabled.load(std::memory_order_relaxed))
        {
            m_OpenGLResourceThread->NotifyWorkAvailable();
            return;
        }

        // Fallback wake already handled above.
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
            bool executingAFrame = false;
            bool shouldShutdown = false;

            // Wait for frame request.
            {
                std::unique_lock<std::mutex> lock(m_RenderThreadMutex);

                m_RenderThreadCV.wait(lock, [this]() {
                    // IMPORTANT:
                    // We must always wake for resource work, even if the optional OpenGL resource thread is enabled.
                    //
                    // Why:
                    // - Many engine subsystems call SubmitResourceAndWait() during startup (VAOs/VBOs/textures/shaders).
                    // - If the resource thread fails to run (driver/SDL quirks, context make-current stalls, etc),
                    //   ignoring resource work here can deadlock the entire engine before the first frame.
                    //
                    // The render thread can always act as a safe fallback consumer for the resource queue.
                    return m_FrameRequested || m_RenderThreadShutdown.load() || !m_PrimaryResourceQueue.IsEmpty() || !m_ResourceQueue.IsEmpty();
                });
                shouldShutdown = m_RenderThreadShutdown.load(std::memory_order_relaxed);


                // Always process any pending resource work before frame execution.
                // (We keep the lock only for the wait/flags; resource processing happens outside.)

                // Consume the request.
                if (m_FrameRequested)
                {
                    m_FrameRequested = false;
                    frameIdToComplete = m_FrameRequestedId;
                    executingAFrame = true;

                    // Snapshot processed totals so we can compute per-frame deltas.
                    m_PrimaryProcessedTotalAtFrameStart = m_PrimaryResourceQueue.GetTotalProcessed();
                    m_SharedProcessedTotalAtFrameStart = m_ResourceQueue.GetTotalProcessed();
                }
            }

            if (shouldShutdown)
            {
                break;
            }

            // Execute resource commands + frame commands under a context-current scope.

            try
            {
                OpenGLContext::ScopedCurrentContext scope(*glContext);

                // Primary-context-only resource work must be drained on the render thread.
                m_PrimaryResourceQueue.Process(m_GraphicsContext, 256);

                // Drain some resource work on the render thread.
                // This acts as:
                // - startup safety net for SubmitResourceAndWait() callers
                // - fallback if the optional OpenGL shared-context resource thread is stalled
                //
                // When the resource thread is healthy, it can also drain in parallel; the queue is MPMC.
                m_ResourceQueue.Process(m_GraphicsContext, 256);

                if (frameIdToComplete != 0)
                {
                m_RenderQueue->ProcessCommands(m_GraphicsContext);
                UpdateGPUMetricsFromOpenGL();
                m_GraphicsContext->SwapBuffers();
                }

                // Record last-frame resource stats when we actually executed/presented a frame.
                if (executingAFrame)
                {
                    const uint64_t primaryProcessedNow = m_PrimaryResourceQueue.GetTotalProcessed();
                    const uint64_t sharedProcessedNow = m_ResourceQueue.GetTotalProcessed();

                    const uint32_t primaryDelta = static_cast<uint32_t>(primaryProcessedNow - m_PrimaryProcessedTotalAtFrameStart);
                    const uint32_t sharedDelta = static_cast<uint32_t>(sharedProcessedNow - m_SharedProcessedTotalAtFrameStart);

                    m_PrimaryProcessedLastFrame.store(primaryDelta, std::memory_order_relaxed);
                    m_SharedProcessedLastFrame.store(sharedDelta, std::memory_order_relaxed);
                    m_PrimaryApproxSizeLastFrame.store(m_PrimaryResourceQueue.GetApproxSize(), std::memory_order_relaxed);
                    m_SharedApproxSizeLastFrame.store(m_ResourceQueue.GetApproxSize(), std::memory_order_relaxed);
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

    Renderer::ResourceQueueStatistics Renderer::GetLastFrameResourceQueueStatistics() const
    {
        ResourceQueueStatistics s{};
        s.PrimaryProcessedLastFrame = m_PrimaryProcessedLastFrame.load(std::memory_order_relaxed);
        s.SharedProcessedLastFrame = m_SharedProcessedLastFrame.load(std::memory_order_relaxed);
        s.PrimaryApproxSize = m_PrimaryApproxSizeLastFrame.load(std::memory_order_relaxed);
        s.SharedApproxSize = m_SharedApproxSizeLastFrame.load(std::memory_order_relaxed);

        s.PrimaryTotalSubmitted = m_PrimaryResourceQueue.GetTotalSubmitted();
        s.PrimaryTotalProcessed = m_PrimaryResourceQueue.GetTotalProcessed();
        s.SharedTotalSubmitted = m_ResourceQueue.GetTotalSubmitted();
        s.SharedTotalProcessed = m_ResourceQueue.GetTotalProcessed();
        return s;
    }

    RenderResourceCommandQueue::DebugLabelSnapshot Renderer::GetPrimaryResourceDebugLabelSnapshot() const
    {
        return m_PrimaryResourceQueue.GetDebugLabelSnapshot();
    }

    RenderResourceCommandQueue::DebugLabelSnapshot Renderer::GetSharedResourceDebugLabelSnapshot() const
    {
        return m_ResourceQueue.GetDebugLabelSnapshot();
    }
} 