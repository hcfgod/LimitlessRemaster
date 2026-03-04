#include "Core/Concurrency/JobSystem.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace Limitless::Concurrency
{
    namespace
    {
        constexpr size_t kInvalidWorkerIndex = std::numeric_limits<size_t>::max();
        thread_local JobSystem* t_CurrentJobSystem = nullptr;
        thread_local size_t t_CurrentWorkerIndex = kInvalidWorkerIndex;
    }

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
        if (!m_Initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            if (threadCount > 1)
                threadCount -= 1;
        }
        if (threadCount == 0)
            threadCount = 1;

        m_ShutdownRequested.store(false, std::memory_order_release);
        m_AcceptingJobs.store(true, std::memory_order_release);
        m_PendingJobs.Value.store(0, std::memory_order_relaxed);

        m_Workers.reserve(threadCount);
        for (size_t index = 0; index < threadCount; ++index)
        {
            m_Workers.emplace_back(std::make_unique<WorkerContext>());
        }
        for (size_t index = 0; index < threadCount; ++index)
        {
            m_Workers[index]->Thread = std::thread(&JobSystem::WorkerMain, this, index);
        }

        LT_CORE_INFO("JobSystem initialized with {} simulation workers", threadCount);
    }

    void JobSystem::Shutdown()
    {
        if (!m_Initialized.load(std::memory_order_acquire))
            return;

        m_AcceptingJobs.store(false, std::memory_order_release);
        Wait();

        m_ShutdownRequested.store(true, std::memory_order_release);
        m_WakeCondition.notify_all();
        for (auto& worker : m_Workers)
        {
            if (worker->Thread.joinable())
                worker->Thread.join();
        }
        m_Workers.clear();

        while (m_InjectorQueue.TryPop().has_value()) {}

        m_Initialized.store(false, std::memory_order_release);
        LT_CORE_INFO("JobSystem shutdown complete");
    }

    bool JobSystem::TrySubmit(std::function<void()> job)
    {
        if (!job)
            return false;

        if (!m_Initialized.load(std::memory_order_acquire) ||
            !m_AcceptingJobs.load(std::memory_order_acquire))
        {
            return false;
        }

        Job queuedJob = std::move(job);
        m_PendingJobs.Value.fetch_add(1, std::memory_order_acq_rel);
        if (TryEnqueue(queuedJob))
        {
            m_WakeCondition.notify_one();
            return true;
        }

        const uint64_t remaining = m_PendingJobs.Value.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            m_IdleCondition.notify_all();

        return false;
    }

    bool JobSystem::Submit(std::function<void()> job)
    {
        if (!job)
            return false;

        Job queuedJob = std::move(job);
        for (;;)
        {
            if (!m_Initialized.load(std::memory_order_acquire) ||
                !m_AcceptingJobs.load(std::memory_order_acquire))
            {
                return false;
            }

            m_PendingJobs.Value.fetch_add(1, std::memory_order_acq_rel);
            if (TryEnqueue(queuedJob))
            {
                m_WakeCondition.notify_one();
                return true;
            }

            const uint64_t remaining = m_PendingJobs.Value.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0)
                m_IdleCondition.notify_all();

            if (!m_Initialized.load(std::memory_order_acquire) ||
                !m_AcceptingJobs.load(std::memory_order_acquire))
            {
                return false;
            }

            std::this_thread::yield();
        }
    }

    void JobSystem::Wait()
    {
        if (!m_Initialized.load(std::memory_order_acquire))
            return;

        std::unique_lock<std::mutex> lock(m_IdleMutex);
        m_IdleCondition.wait(lock, [this]() {
            return m_PendingJobs.Value.load(std::memory_order_acquire) == 0;
        });
    }

    std::optional<JobSystem::Job> JobSystem::TryAcquireJob(size_t workerIndex)
    {
        if (workerIndex >= m_Workers.size())
            return std::nullopt;

        if (auto local = m_Workers[workerIndex]->LocalQueue.Pop(); local.has_value())
            return local;

        if (auto injected = m_InjectorQueue.TryPop(); injected.has_value())
            return injected;

        const size_t workerCount = m_Workers.size();
        for (size_t offset = 1; offset < workerCount; ++offset)
        {
            const size_t victim = (workerIndex + offset) % workerCount;
            if (auto stolen = m_Workers[victim]->LocalQueue.Steal(); stolen.has_value())
                return stolen;
        }

        return std::nullopt;
    }

    bool JobSystem::TryEnqueue(Job& job)
    {
        if (t_CurrentJobSystem == this &&
            t_CurrentWorkerIndex != kInvalidWorkerIndex &&
            t_CurrentWorkerIndex < m_Workers.size())
        {
            if (m_Workers[t_CurrentWorkerIndex]->LocalQueue.TryPush(std::move(job)))
                return true;
        }

        if (m_InjectorQueue.TryPush(std::move(job)))
            return true;

        return false;
    }

    void JobSystem::CompleteOneJob()
    {
        const uint64_t remaining = m_PendingJobs.Value.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            m_IdleCondition.notify_all();
    }

    void JobSystem::WorkerMain(size_t workerIndex)
    {
        t_CurrentJobSystem = this;
        t_CurrentWorkerIndex = workerIndex;

        for (;;)
        {
            auto job = TryAcquireJob(workerIndex);
            if (!job.has_value())
            {
                if (m_ShutdownRequested.load(std::memory_order_acquire) &&
                    m_PendingJobs.Value.load(std::memory_order_acquire) == 0)
                {
                    break;
                }

                std::unique_lock<std::mutex> lock(m_WakeMutex);
                m_WakeCondition.wait(lock, [this]() {
                    return m_ShutdownRequested.load(std::memory_order_acquire) ||
                        m_PendingJobs.Value.load(std::memory_order_acquire) > 0;
                });

                if (m_ShutdownRequested.load(std::memory_order_acquire) &&
                    m_PendingJobs.Value.load(std::memory_order_acquire) == 0)
                {
                    break;
                }

                continue;
            }

            try
            {
                if (*job)
                    (*job)();
            }
            catch (const std::exception& exception)
            {
                LT_CORE_ERROR("JobSystem worker exception: {}", exception.what());
            }
            catch (...)
            {
                LT_CORE_ERROR("JobSystem worker exception: unknown error");
            }

            CompleteOneJob();
        }

        t_CurrentWorkerIndex = kInvalidWorkerIndex;
        t_CurrentJobSystem = nullptr;
    }
}
