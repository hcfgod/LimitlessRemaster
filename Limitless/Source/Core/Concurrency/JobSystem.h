#pragma once

#include "Core/Concurrency/LockFreeQueue.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
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
        uint32_t m_Remaining = 0;
        std::mutex m_Mutex;
        std::condition_variable m_Condition;
    };

    class JobSystem
    {
    public:
        static JobSystem& GetInstance();

        void Initialize(size_t threadCount = 0);
        void Shutdown();

        bool IsInitialized() const { return m_Initialized.load(std::memory_order_acquire); }
        size_t GetWorkerCount() const { return m_Workers.size(); }

        bool TrySubmit(std::function<void()> job);
        bool Submit(std::function<void()> job);
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

            using FunctionType = std::decay_t<Func>;
            static_assert(std::is_copy_constructible_v<FunctionType>,
                "JobSystem::ParallelFor requires a copy-constructible callable");
            FunctionType functionCopy = std::forward<Func>(function);

            WaitGroup waitGroup;
            for (size_t chunkBegin = beginIndex; chunkBegin < endIndex; chunkBegin += safeGrain)
            {
                const size_t chunkEnd = std::min(endIndex, chunkBegin + safeGrain);
                waitGroup.Add(1);
                const bool submitted = Submit([chunkBegin, chunkEnd, fn = functionCopy, &waitGroup]() mutable {
                    struct WaitGroupDoneGuard final
                    {
                        WaitGroup& Group;
                        ~WaitGroupDoneGuard() { Group.Done(); }
                    } doneGuard{ waitGroup };

                    for (size_t index = chunkBegin; index < chunkEnd; ++index)
                        fn(index);
                });
                if (!submitted)
                {
                    for (size_t index = chunkBegin; index < chunkEnd; ++index)
                        functionCopy(index);
                    waitGroup.Done();
                }
            }
            waitGroup.Wait();
        }

    private:
        JobSystem() = default;
        ~JobSystem();
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        using Job = std::shared_ptr<std::function<void()>>;
        static constexpr size_t kWorkerQueueSize = 1024;
        static constexpr size_t kInjectorQueueSize = 8192;

        struct WorkerContext
        {
            WorkStealingQueue<Job, kWorkerQueueSize> LocalQueue;
            std::thread Thread;
        };

        void WorkerMain(size_t workerIndex);
        std::optional<Job> TryAcquireJob(size_t workerIndex);
        bool TryEnqueue(Job& job);
        void CompleteOneJob();

    private:
        struct alignas(64) PaddedAtomicU64
        {
            std::atomic<uint64_t> Value{ 0 };
        };

        std::vector<std::unique_ptr<WorkerContext>> m_Workers;
        LockFreeMPMCQueue<Job, kInjectorQueueSize> m_InjectorQueue;
        std::mutex m_WakeMutex;
        std::condition_variable m_WakeCondition;
        std::mutex m_IdleMutex;
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
