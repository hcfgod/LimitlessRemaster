#pragma once

#include "Graphics/RenderResourceCommandQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace Limitless
{
    class GraphicsContext;
    class SharedResourceContext;

    /// Dedicated GPU resource execution thread.
    ///
    /// This thread owns a SharedResourceContext (backend-neutral secondary context)
    /// and drains `RenderResourceCommandQueue`.
    ///
    /// Why:
    /// - Overlap GPU resource uploads/creates/deletes with frame rendering.
    /// - Keep draw/state execution deterministic and single-threaded on the render thread.
    ///
    /// Important contract:
    /// - Only GPU resource operations belong here.
    /// - If a caller needs a resource to be immediately safe-to-use on the render context, it must
    ///   use a submission helper that performs synchronization before signaling completion.
    class RenderResourceThread final
    {
    public:
        RenderResourceThread(RenderResourceCommandQueue& queue,
                             GraphicsContext* primaryGraphicsContext,
                             std::unique_ptr<SharedResourceContext> sharedContext);
        ~RenderResourceThread();

        RenderResourceThread(const RenderResourceThread&) = delete;
        RenderResourceThread& operator=(const RenderResourceThread&) = delete;

        void Start();
        void Stop();

        bool IsRunning() const { return m_Running.load(std::memory_order_relaxed); }

        // Wake the resource thread if it is waiting.
        void NotifyWorkAvailable();

    private:
        void ThreadMain();

    private:
        RenderResourceCommandQueue& m_Queue;
        GraphicsContext* m_PrimaryGraphicsContext = nullptr; // Borrowed, used only as an execution parameter for commands.
        std::unique_ptr<SharedResourceContext> m_SharedContext;

        std::atomic<bool> m_Running{false};
        std::atomic<bool> m_Shutdown{false};
        std::thread m_Thread;

        std::mutex m_Mutex;
        std::condition_variable m_CV;
    };
}

