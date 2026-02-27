#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace Limitless::Concurrency
{
    class WaitGroup
    {
    public:
        void Add(uint32_t count = 1);
        void Done();
        void Wait();

    private:
        std::atomic<uint32_t> m_Remaining{ 0 };
        std::mutex m_Mutex;
        std::condition_variable m_Condition;
    };

    class JobSystem
    {
    public:
        static JobSystem& GetInstance();

        void Initialize(size_t threadCount = 0);
        void Shutdown();

        bool IsInitialized() const { return m_Initialized.load(std::memory_order_relaxed); }
        size_t GetWorkerCount() const { return m_Workers.size(); }

        void Submit(std::function<void()> job);
        void Wait();

        template<typename Func>
        void ParallelFor(size_t beginIndex, size_t endIndex, size_t grainSize, Func&& function)
        {
            if (beginIndex >= endIndex)
                return;

            if (!IsInitialized())
            {
                for (size_t index = beginIndex; index < endIndex; ++index)
                    function(index);
                return;
            }

            const size_t itemCount = endIndex - beginIndex;
            size_t safeGrain = grainSize;
            if (safeGrain == 0)
            {
                const size_t suggestedChunks = std::max<size_t>(1, GetWorkerCount() * 4);
                safeGrain = std::max<size_t>(1, itemCount / suggestedChunks);
            }

            WaitGroup waitGroup;
            for (size_t chunkBegin = beginIndex; chunkBegin < endIndex; chunkBegin += safeGrain)
            {
                const size_t chunkEnd = std::min(endIndex, chunkBegin + safeGrain);
                waitGroup.Add(1);
                Submit([chunkBegin, chunkEnd, fn = std::forward<Func>(function), &waitGroup]() mutable {
                    for (size_t index = chunkBegin; index < chunkEnd; ++index)
                        fn(index);
                    waitGroup.Done();
                });
            }
            waitGroup.Wait();
        }

    private:
        JobSystem() = default;
        ~JobSystem();
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        void WorkerMain();

    private:
        struct alignas(64) PaddedAtomicU64
        {
            std::atomic<uint64_t> Value{ 0 };
        };

        std::vector<std::thread> m_Workers;
        std::vector<std::function<void()>> m_Queue;
        std::mutex m_QueueMutex;
        std::condition_variable m_QueueCondition;
        std::condition_variable m_IdleCondition;
        std::atomic<bool> m_Initialized{ false };
        std::atomic<bool> m_ShutdownRequested{ false };
        std::atomic<bool> m_AcceptingJobs{ false };
        PaddedAtomicU64 m_PendingJobs;
    };

    inline JobSystem& GetJobSystem()
    {
        return JobSystem::GetInstance();
    }
}
