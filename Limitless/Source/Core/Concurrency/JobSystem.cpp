#include "Core/Concurrency/JobSystem.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <exception>

namespace Limitless::Concurrency
{
    void WaitGroup::Add(uint32_t count)
    {
        if (count == 0)
            return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Remaining += count;
    }

    void WaitGroup::Done()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Remaining == 0)
            return;
        --m_Remaining;
        if (m_Remaining == 0)
        {
            m_Condition.notify_all();
        }
    }

    void WaitGroup::Wait()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Condition.wait(lock, [this]() {
            return m_Remaining == 0;
        });
    }

    JobSystem& JobSystem::GetInstance()
    {
        static JobSystem instance;
        return instance;
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    void JobSystem::Initialize(size_t threadCount)
    {
        bool expected = false;
        if (!m_Initialized.compare_exchange_strong(expected, true))
            return;

        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            if (threadCount > 1)
                threadCount -= 1;
        }
        if (threadCount == 0)
            threadCount = 1;

        m_ShutdownRequested.store(false, std::memory_order_relaxed);
        m_AcceptingJobs.store(true, std::memory_order_relaxed);
        m_PendingJobs.Value.store(0, std::memory_order_relaxed);

        m_Queue.reserve(2048);
        m_Workers.reserve(threadCount);
        for (size_t index = 0; index < threadCount; ++index)
            m_Workers.emplace_back(&JobSystem::WorkerMain, this);

        LT_CORE_INFO("JobSystem initialized with {} simulation workers", threadCount);
    }

    void JobSystem::Shutdown()
    {
        if (!m_Initialized.load(std::memory_order_relaxed))
            return;

        m_AcceptingJobs.store(false, std::memory_order_relaxed);
        Wait();

        m_ShutdownRequested.store(true, std::memory_order_relaxed);
        m_QueueCondition.notify_all();
        for (auto& worker : m_Workers)
        {
            if (worker.joinable())
                worker.join();
        }
        m_Workers.clear();

        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            m_Queue.clear();
        }

        m_Initialized.store(false, std::memory_order_relaxed);
        LT_CORE_INFO("JobSystem shutdown complete");
    }

    void JobSystem::Submit(std::function<void()> job)
    {
        if (!job)
            return;

        if (!m_Initialized.load(std::memory_order_relaxed) ||
            !m_AcceptingJobs.load(std::memory_order_relaxed))
        {
            job();
            return;
        }

        m_PendingJobs.Value.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_QueueMutex);
            m_Queue.emplace_back(std::move(job));
        }
        m_QueueCondition.notify_one();
    }

    void JobSystem::Wait()
    {
        if (!m_Initialized.load(std::memory_order_relaxed))
            return;

        std::unique_lock<std::mutex> lock(m_QueueMutex);
        m_IdleCondition.wait(lock, [this]() {
            return m_PendingJobs.Value.load(std::memory_order_acquire) == 0 && m_Queue.empty();
        });
    }

    void JobSystem::WorkerMain()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);
                m_QueueCondition.wait(lock, [this]() {
                    return m_ShutdownRequested.load(std::memory_order_relaxed) || !m_Queue.empty();
                });

                if (m_ShutdownRequested.load(std::memory_order_relaxed) && m_Queue.empty())
                    return;

                job = std::move(m_Queue.back());
                m_Queue.pop_back();
            }

            try
            {
                job();
            }
            catch (const std::exception& exception)
            {
                LT_CORE_ERROR("JobSystem worker exception: {}", exception.what());
            }
            catch (...)
            {
                LT_CORE_ERROR("JobSystem worker exception: unknown error");
            }

            const uint64_t remaining = m_PendingJobs.Value.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0)
            {
                std::lock_guard<std::mutex> lock(m_QueueMutex);
                m_IdleCondition.notify_all();
            }
        }
    }
}
