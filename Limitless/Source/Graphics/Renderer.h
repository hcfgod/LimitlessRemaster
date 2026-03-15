#pragma once

#include "GraphicsContext.h"
#include "RenderCommandQueue.h"
#include "RenderResourceCommandQueue.h"
#include "RenderResourceThread.h"
#include "FrameUploadAllocator.h"
#include "FrameCommandArena.h"
#include "Core/Debug/Log.h"
#include <deque>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

namespace Limitless
{
    class Renderer
    {
    public:
        enum class ResourceRetirementContext
        {
            Shared,
            Primary
        };

        static Renderer& GetInstance();
        
        // Initialize the renderer with a graphics context (borrowed, not owned)
        void Initialize(GraphicsContext* context);
        
        // Shutdown the renderer
        void Shutdown();
        
        // Check if renderer is initialized
        bool IsInitialized() const { return m_GraphicsContext != nullptr; }
        
        // Get the graphics context
        GraphicsContext* GetGraphicsContext() const { return m_GraphicsContext; }
        GraphicsAPI GetActiveAPI() const;
        
        // Get the render command queue
        RenderCommandQueue* GetRenderQueue() const { return m_RenderQueue.get(); }
        
        // Submit a render command
        bool SubmitCommand(std::unique_ptr<RenderCommand> command);
        bool SubmitCommand(UniqueRenderCommand command);
        
        // Submit a render command with priority
        bool SubmitCommandWithPriority(std::unique_ptr<RenderCommand> command, RenderCommandPriority priority);
        bool SubmitCommandWithPriority(UniqueRenderCommand command, RenderCommandPriority priority);
        
        // Execute a command immediately
        void ExecuteImmediate(std::unique_ptr<RenderCommand> command);
        void ExecuteImmediate(UniqueRenderCommand command);
        
        // Process all queued commands
        void ProcessCommands();
        
        // Begin a new frame
        void BeginFrame();
        
        // End the current frame
        void EndFrame();
        
        // Swap buffers
        void SwapBuffers();

        void SetViewport(int x, int y, int width, int height);

        // Allocate CPU-side upload staging bytes for this frame.
        // Intended for render-command payloads that must outlive the submission thread until execution.
        void* AllocateFrameUpload(size_t sizeBytes, size_t alignment = 16);

        // Submit a command allocated from the renderer's frame command arena.
        // This avoids per-command heap allocations in hot paths.
        template<typename TCommand, typename... Args>
        bool SubmitCommandArena(Args&&... args)
        {
            return SubmitCommand(m_FrameCommandArena.Make<TCommand>(std::forward<Args>(args)...));
        }

        // Render thread control
        // When enabled, the render thread owns "process commands + present" for each frame.
        // The main thread still builds/submits commands. `SwapBuffers()` will block until the
        // render thread completes the frame.
        void EnableRenderThread(bool enable);
        bool IsRenderThreadEnabled() const { return m_RenderThreadEnabled; }

        // Non-blocking resource submissions.
        // These enqueue work on the render thread resource queue and return immediately.
        // Use these for large uploads to avoid stalling the calling thread.
        bool SubmitResource(std::unique_ptr<RenderResourceCommandQueue::Command> command);

        // Primary-context-only resource submission helpers.
        //
        // IMPORTANT:
        // Some OpenGL object types are NOT shared across contexts even when context sharing is enabled
        // (notably: Vertex Array Objects, and typically FBO-related state).
        //
        // Any GPU work that creates/modifies such objects must be executed on the **primary** OpenGL context,
        // which this engine owns on the render thread.
        template<typename Func>
        auto SubmitPrimaryResourceAndWait(const char* debugName, Func&& func) -> decltype(func(static_cast<GraphicsContext*>(nullptr)))
        {
            using ResultT = decltype(func(static_cast<GraphicsContext*>(nullptr)));

            // Fast path: if the calling thread already owns the graphics context
            // (either the render thread or the main thread inside ProcessCommands),
            // execute inline to avoid a cross-thread round-trip and prevent deadlocks.
            if (IsOnRenderThread() || (m_GraphicsContext && m_GraphicsContext->IsCurrentOnThisThread()))
            {
                if constexpr (std::is_void_v<ResultT>)
                {
                    func(m_GraphicsContext);
                    return;
                }
                else
                {
                    return func(m_GraphicsContext);
                }
            }

            if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("SubmitPrimaryResourceAndWait requires the render thread to be running");
            }

            struct SharedState
            {
                std::promise<ResultT> promise;
                std::function<ResultT(GraphicsContext*)> function;
                const char* debugName = "PrimaryResource";
            };

            auto state = std::make_shared<SharedState>();
            state->function = std::forward<Func>(func);
            state->debugName = (debugName && debugName[0] != '\0') ? debugName : "PrimaryResource";
            std::future<ResultT> future = state->promise.get_future();

            class CommandImpl final : public RenderResourceCommandQueue::Command
            {
            public:
                explicit CommandImpl(std::shared_ptr<SharedState> shared)
                    : m_Shared(std::move(shared))
                {
                }

                const char* GetDebugName() const override
                {
                    return m_Shared->debugName;
                }

                void Execute(GraphicsContext* context) override
                {
                    try
                    {
                        if constexpr (std::is_void_v<ResultT>)
                        {
                            m_Shared->function(context);
                            m_Shared->promise.set_value();
                        }
                        else
                        {
                            ResultT result = m_Shared->function(context);
                            m_Shared->promise.set_value(std::move(result));
                        }
                    }
                    catch (...)
                    {
                        m_Shared->promise.set_exception(std::current_exception());
                    }
                }

            private:
                std::shared_ptr<SharedState> m_Shared;
            };

            if (!m_PrimaryResourceQueue.Submit(std::make_unique<CommandImpl>(state)))
            {
                throw std::runtime_error("RenderResourceCommandQueue (primary) is full");
            }

            // Wake the render thread; it is the only consumer of the primary-resource queue.
            NotifyRenderThreadResourceWorkAvailable();

            if constexpr (std::is_void_v<ResultT>)
            {
                future.get();
            }
            else
            {
                return future.get();
            }
        }

        template<typename Func>
        auto SubmitPrimaryResourceAndWait(Func&& func) -> decltype(func(static_cast<GraphicsContext*>(nullptr)))
        {
            return SubmitPrimaryResourceAndWait("PrimaryResource", std::forward<Func>(func));
        }

        template<typename Func>
        std::future<decltype(std::declval<Func>()(static_cast<GraphicsContext*>(nullptr)))> SubmitPrimaryResourceAsync(const char* debugName, Func&& func)
        {
            using ResultT = decltype(func(static_cast<GraphicsContext*>(nullptr)));

            if (IsOnRenderThread() || (m_GraphicsContext && m_GraphicsContext->IsCurrentOnThisThread()))
            {
                std::promise<ResultT> promise;
                auto future = promise.get_future();
                try
                {
                    if constexpr (std::is_void_v<ResultT>)
                    {
                        func(m_GraphicsContext);
                        promise.set_value();
                    }
                    else
                    {
                        promise.set_value(func(m_GraphicsContext));
                    }
                }
                catch (...)
                {
                    promise.set_exception(std::current_exception());
                }
                return future;
            }

            if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("SubmitPrimaryResourceAsync requires the render thread to be running");
            }

            struct SharedState
            {
                std::promise<ResultT> promise;
                std::function<ResultT(GraphicsContext*)> function;
                const char* debugName = "PrimaryResource";
            };

            auto state = std::make_shared<SharedState>();
            state->function = std::forward<Func>(func);
            state->debugName = (debugName && debugName[0] != '\0') ? debugName : "PrimaryResource";
            std::future<ResultT> future = state->promise.get_future();

            class CommandImpl final : public RenderResourceCommandQueue::Command
            {
            public:
                explicit CommandImpl(std::shared_ptr<SharedState> shared)
                    : m_Shared(std::move(shared))
                {
                }

                const char* GetDebugName() const override
                {
                    return m_Shared->debugName;
                }

                void Execute(GraphicsContext* context) override
                {
                    try
                    {
                        if constexpr (std::is_void_v<ResultT>)
                        {
                            m_Shared->function(context);
                            m_Shared->promise.set_value();
                        }
                        else
                        {
                            ResultT result = m_Shared->function(context);
                            m_Shared->promise.set_value(std::move(result));
                        }
                    }
                    catch (...)
                    {
                        m_Shared->promise.set_exception(std::current_exception());
                    }
                }

            private:
                std::shared_ptr<SharedState> m_Shared;
            };

            if (!m_PrimaryResourceQueue.Submit(std::make_unique<CommandImpl>(state)))
            {
                throw std::runtime_error("RenderResourceCommandQueue (primary) is full");
            }

            NotifyRenderThreadResourceWorkAvailable();
            return future;
        }

        template<typename Func>
        std::future<decltype(std::declval<Func>()(static_cast<GraphicsContext*>(nullptr)))> SubmitPrimaryResourceAsync(Func&& func)
        {
            return SubmitPrimaryResourceAsync("PrimaryResource", std::forward<Func>(func));
        }

        // Returns true when the OpenGL shared-context resource thread is active.
        // When enabled, GPU resource work executes on a dedicated thread with its own shared
        // OpenGL context (in parallel with frame rendering on the render thread).
        bool IsOpenGLResourceThreadEnabled() const { return m_OpenGLResourceThreadEnabled.load(std::memory_order_relaxed); }

        // -------------------------------------------------------------------------
        // Resource queue statistics (debug/telemetry)
        // -------------------------------------------------------------------------
        struct ResourceQueueStatistics
        {
            // Processed during the most recently completed frame on the render thread.
            uint32_t PrimaryProcessedLastFrame = 0;
            uint32_t SharedProcessedLastFrame = 0;

            // Approx queue depths at the time the frame completed.
            uint32_t PrimaryApproxSize = 0;
            uint32_t SharedApproxSize = 0;

            // Totals since startup (monotonic).
            uint64_t PrimaryTotalSubmitted = 0;
            uint64_t PrimaryTotalProcessed = 0;
            uint64_t SharedTotalSubmitted = 0;
            uint64_t SharedTotalProcessed = 0;

            // GPU resource retirement queue depth.
            uint32_t PendingRetirementCount = 0;
        };

        // Thread-safe snapshot of last-frame resource work.
        ResourceQueueStatistics GetLastFrameResourceQueueStatistics() const;

        // Per-label telemetry snapshots (allocation-free).
        RenderResourceCommandQueue::DebugLabelSnapshot GetPrimaryResourceDebugLabelSnapshot() const;
        RenderResourceCommandQueue::DebugLabelSnapshot GetSharedResourceDebugLabelSnapshot() const;

        template<typename Func>
        std::future<decltype(std::declval<Func>()(static_cast<GraphicsContext*>(nullptr)))> SubmitResourceAsync(const char* debugName, Func&& func)
        {
            using ResultT = decltype(func(static_cast<GraphicsContext*>(nullptr)));

            if (IsOnRenderThread() || (m_GraphicsContext && m_GraphicsContext->IsCurrentOnThisThread()))
            {
                std::promise<ResultT> promise;
                auto fut = promise.get_future();
                try
                {
                    if constexpr (std::is_void_v<ResultT>)
                    {
                        func(m_GraphicsContext);
                        promise.set_value();
                    }
                    else
                    {
                        promise.set_value(func(m_GraphicsContext));
                    }
                }
                catch (...)
                {
                    promise.set_exception(std::current_exception());
                }
                return fut;
            }

            if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("SubmitResourceAsync requires the render thread to be running");
            }

            struct SharedState
            {
                std::promise<ResultT> promise;
                std::function<ResultT(GraphicsContext*)> function;
                bool RequiresCrossContextSynchronization = false;
                const char* debugName = "Resource";
            };

            auto state = std::make_shared<SharedState>();
            state->function = std::forward<Func>(func);
            state->RequiresCrossContextSynchronization = IsOpenGLResourceThreadEnabled();
            state->debugName = (debugName && debugName[0] != '\0') ? debugName : "Resource";
            std::future<ResultT> future = state->promise.get_future();

            class CommandImpl final : public RenderResourceCommandQueue::Command
            {
            public:
                explicit CommandImpl(std::shared_ptr<SharedState> shared)
                    : m_Shared(std::move(shared))
                {
                }

                const char* GetDebugName() const override
                {
                    return m_Shared->debugName;
                }

                void Execute(GraphicsContext* context) override
                {
                    try
                    {
                        if constexpr (std::is_void_v<ResultT>)
                        {
                            m_Shared->function(context);
                            if (m_Shared->RequiresCrossContextSynchronization)
                            {
                                Renderer::SynchronizeOpenGLResourceWorkForCrossContextVisibility();
                            }
                            m_Shared->promise.set_value();
                        }
                        else
                        {
                            ResultT result = m_Shared->function(context);
                            if (m_Shared->RequiresCrossContextSynchronization)
                            {
                                Renderer::SynchronizeOpenGLResourceWorkForCrossContextVisibility();
                            }
                            m_Shared->promise.set_value(std::move(result));
                        }
                    }
                    catch (...)
                    {
                        m_Shared->promise.set_exception(std::current_exception());
                    }
                }

            private:
                std::shared_ptr<SharedState> m_Shared;
            };

            if (!SubmitResource(std::make_unique<CommandImpl>(state)))
            {
                throw std::runtime_error("RenderResourceCommandQueue is full");
            }

            return future;
        }

        template<typename Func>
        std::future<decltype(std::declval<Func>()(static_cast<GraphicsContext*>(nullptr)))> SubmitResourceAsync(Func&& func)
        {
            return SubmitResourceAsync("Resource", std::forward<Func>(func));
        }

        // Resource command submission (GPU resource operations).
        // If called from the render thread, the callable executes inline.
        template<typename Func>
        auto SubmitResourceAndWait(const char* debugName, Func&& func) -> decltype(func(static_cast<GraphicsContext*>(nullptr)))
        {
            using ResultT = decltype(func(static_cast<GraphicsContext*>(nullptr)));
            // Fast path: if the calling thread already owns the graphics context
            // (either the render thread or the main thread inside ProcessCommands),
            // execute inline to avoid a cross-thread round-trip and prevent deadlocks.
            if (IsOnRenderThread() || (m_GraphicsContext && m_GraphicsContext->IsCurrentOnThisThread()))
            {
                if constexpr (std::is_void_v<ResultT>)
                {
                    func(m_GraphicsContext);
                    return;
                }
                else
                {
                    return func(m_GraphicsContext);
                }
            }

            if (!m_RenderThreadRunning.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("SubmitResourceAndWait requires the render thread to be running");
            }

            struct SharedState
            {
                std::promise<ResultT> promise;
                std::function<ResultT(GraphicsContext*)> function;
                bool RequiresCrossContextSynchronization = false;
                const char* debugName = "Resource";
            };

            auto state = std::make_shared<SharedState>();
            state->function = std::forward<Func>(func);
            state->RequiresCrossContextSynchronization = IsOpenGLResourceThreadEnabled();
            state->debugName = (debugName && debugName[0] != '\0') ? debugName : "Resource";
            std::future<ResultT> future = state->promise.get_future();

            class CommandImpl final : public RenderResourceCommandQueue::Command
            {
            public:
                explicit CommandImpl(std::shared_ptr<SharedState> shared)
                    : m_Shared(std::move(shared))
                {
                }

                const char* GetDebugName() const override
                {
                    return m_Shared->debugName;
                }

                void Execute(GraphicsContext* context) override
                {
                    try
                    {
                        if constexpr (std::is_void_v<ResultT>)
                        {
                            m_Shared->function(context);
                            if (m_Shared->RequiresCrossContextSynchronization)
                            {
                                Renderer::SynchronizeOpenGLResourceWorkForCrossContextVisibility();
                            }
                            m_Shared->promise.set_value();
                        }
                        else
                        {
                            ResultT result = m_Shared->function(context);
                            if (m_Shared->RequiresCrossContextSynchronization)
                            {
                                Renderer::SynchronizeOpenGLResourceWorkForCrossContextVisibility();
                            }
                            m_Shared->promise.set_value(std::move(result));
                        }
                    }
                    catch (...)
                    {
                        m_Shared->promise.set_exception(std::current_exception());
                    }
                }

            private:
                std::shared_ptr<SharedState> m_Shared;
            };

            if (!m_ResourceQueue.Submit(std::make_unique<CommandImpl>(state)))
            {
                throw std::runtime_error("RenderResourceCommandQueue is full");
            }

            NotifyResourceWorkAvailable();

            if constexpr (std::is_void_v<ResultT>)
            {
                future.get();
            }
            else
            {
                return future.get();
            }
        }

        template<typename Func>
        auto SubmitResourceAndWait(Func&& func) -> decltype(func(static_cast<GraphicsContext*>(nullptr)))
        {
            return SubmitResourceAndWait("Resource", std::forward<Func>(func));
        }

        bool RetireResource(const char* debugName,
                            ResourceRetirementContext context,
                            std::function<void(GraphicsContext*)> callback);

    private:
        Renderer() = default;
        ~Renderer() = default;
        
        // Disable copy and assignment
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
        
        RenderResourceCommandQueue m_PrimaryResourceQueue;
        RenderResourceCommandQueue m_ResourceQueue;

        GraphicsContext* m_GraphicsContext = nullptr; // Borrowed, not owned
        std::unique_ptr<RenderCommandQueue> m_RenderQueue;

        // Dedicated render thread model (OpenGL-friendly):
        // - Submission: any thread (RenderCommandQueue is MPMC)
        // - Execution/present: render thread owns "frame execution + SwapBuffers"
        std::thread m_RenderThread;

        uint64_t m_FrameRequestedId = 0;
        uint64_t m_FrameCompletedId = 0;
        std::thread::id m_RenderThreadId{};

        // Totals used to compute per-frame deltas.
        uint64_t m_PrimaryProcessedTotalAtFrameStart = 0;
        uint64_t m_SharedProcessedTotalAtFrameStart = 0;

        // Optional OpenGL shared-context resource thread (multi-threaded GPU resource execution).
        std::unique_ptr<RenderResourceThread> m_OpenGLResourceThread;

        uint64_t m_FrameUploadFrameId = 0;
        std::atomic<uint64_t> m_ResourceRetirementSubmissionFrameId{0};
        std::atomic<uint64_t> m_ResourceRetirementCompletedFrameId{0};

        // Frame-local upload staging (reduces per-upload heap allocations).
        FrameUploadAllocator m_FrameUploadAllocator;

        FrameCommandArena m_FrameCommandArena;

        std::mutex m_RenderThreadMutex;
        std::condition_variable m_RenderThreadCV;

        // Last-frame resource stats (written by render thread, read by others).
        std::atomic<uint32_t> m_PrimaryProcessedLastFrame{0};
        std::atomic<uint32_t> m_SharedProcessedLastFrame{0};
        std::atomic<uint32_t> m_PrimaryApproxSizeLastFrame{0};
        std::atomic<uint32_t> m_SharedApproxSizeLastFrame{0};
        std::atomic<uint32_t> m_PendingResourceRetirementCount{0};

        struct ResourceRetirementEntry
        {
            uint64_t TargetCompletedFrameId = 0;
            ResourceRetirementContext Context = ResourceRetirementContext::Shared;
            const char* DebugName = "RetiredResource";
            std::function<void(GraphicsContext*)> Callback;
        };

        std::mutex m_ResourceRetirementMutex;
        std::deque<ResourceRetirementEntry> m_PendingResourceRetirements;

        bool m_Initialized = false;
        std::atomic<bool> m_RenderThreadEnabled{false};
        std::atomic<bool> m_RenderThreadRunning{false};
        std::atomic<bool> m_RenderThreadShutdown{false};
        
        bool m_FrameRequested = false;
        std::atomic<bool> m_OpenGLResourceThreadEnabled{false};

        void StartRenderThread();
        void StopRenderThread();
        void RenderThreadMain();
        void ProcessPendingResourceRetirements(GraphicsContext* context, bool forceAll);

        bool IsOnRenderThread() const { return m_RenderThreadRunning.load() && (std::this_thread::get_id() == m_RenderThreadId); }
        void NotifyRenderThreadResourceWorkAvailable();
        void NotifyResourceWorkAvailable();

        // When a shared OpenGL context is used for resource work, the producer context must
        // synchronize before a resource is safe to use in the consumer context.
        // This must be called while an OpenGL context is current on the calling thread.
        static void SynchronizeOpenGLResourceWorkForCrossContextVisibility();
    };
} 