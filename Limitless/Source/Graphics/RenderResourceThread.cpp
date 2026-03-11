#include "Graphics/RenderResourceThread.h"

#include "Core/Debug/Log.h"
#include "Graphics/OpenGL/OpenGLSharedContext.h"

#include <stdexcept>

namespace Limitless
{
    RenderResourceThread::RenderResourceThread(RenderResourceCommandQueue& queue,
                                               GraphicsContext* primaryGraphicsContext,
                                               std::unique_ptr<OpenGLSharedContext> sharedContext)
        : m_Queue(queue)
        , m_PrimaryGraphicsContext(primaryGraphicsContext)
        , m_SharedContext(std::move(sharedContext))
    {
        if (!m_PrimaryGraphicsContext)
        {
            throw std::runtime_error("RenderResourceThread: primary GraphicsContext is null");
        }
        if (!m_SharedContext)
        {
            throw std::runtime_error("RenderResourceThread: shared OpenGL context is null");
        }
    }

    RenderResourceThread::~RenderResourceThread()
    {
        Stop();
    }

    void RenderResourceThread::Start()
    {
        if (m_Running.load(std::memory_order_relaxed))
        {
            return;
        }

        m_Shutdown.store(false, std::memory_order_relaxed);
        m_Running.store(true, std::memory_order_relaxed);
        m_Thread = std::thread(&RenderResourceThread::ThreadMain, this);
        LT_CORE_INFO("RenderResourceThread: started");
    }

    void RenderResourceThread::Stop()
    {
        if (!m_Running.load(std::memory_order_relaxed))
        {
            return;
        }

        m_Shutdown.store(true, std::memory_order_relaxed);
        NotifyWorkAvailable();

        if (m_Thread.joinable())
        {
            m_Thread.join();
        }

        m_Running.store(false, std::memory_order_relaxed);
        LT_CORE_INFO("RenderResourceThread: stopped");
    }

    void RenderResourceThread::NotifyWorkAvailable()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CV.notify_one();
    }

    void RenderResourceThread::ThreadMain()
    {
        for (;;)
        {
            bool shouldShutdown = false;
            // Wait for work or shutdown request.
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CV.wait(lock, [this]() {
                    return m_Shutdown.load(std::memory_order_relaxed) || !m_Queue.IsEmpty();
                });
                shouldShutdown = m_Shutdown.load(std::memory_order_relaxed);
            }

            if (shouldShutdown)
            {
                break;
            }

            try
            {
                // Make the shared context current for this thread and drain resource work.
                OpenGLSharedContext::ScopedCurrentContext scope(*m_SharedContext);
                m_Queue.Process(m_PrimaryGraphicsContext, 2048);
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("RenderResourceThread: exception: {}", e.what());
            }
        }
    }
}

