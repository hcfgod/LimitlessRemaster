#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Core/Concurrency/JobSystem.h"
#include "Core/Concurrency/LockFreeQueue.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

TEST_SUITE("Correctness Contracts - Concurrency Queues")
{
    TEST_CASE("LockFreeSPSCQueue preserves FIFO order under SPSC")
    {
        using Queue = Limitless::Concurrency::LockFreeSPSCQueue<int, 1024>;
        Queue queue;

        constexpr int kCount = 10000;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        std::vector<int> out;
        out.reserve(kCount);

        std::thread producer([&]()
        {
            for (int i = 0; i < kCount; ++i)
            {
                while (!queue.TryPush(int{i}))
                {
                    // bounded queue: spin until consumer pops
                    std::this_thread::yield();
                }
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::thread consumer([&]()
        {
            while (consumed.load(std::memory_order_relaxed) < kCount)
            {
                auto value = queue.TryPop();
                if (!value)
                {
                    std::this_thread::yield();
                    continue;
                }

                out.push_back(*value);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });

        producer.join();
        consumer.join();

        CHECK(produced.load() == kCount);
        CHECK(consumed.load() == kCount);
        REQUIRE(out.size() == static_cast<size_t>(kCount));

        for (int i = 0; i < kCount; ++i)
        {
            CHECK(out[static_cast<size_t>(i)] == i);
        }
    }

    TEST_CASE("LockFreeSPSCQueue capacity is Size-1")
    {
        // Size must be power of two. Capacity of this SPSC queue is Size-1.
        using Queue = Limitless::Concurrency::LockFreeSPSCQueue<int, 8>;
        Queue queue;

        // Push 7 items should succeed.
        for (int i = 0; i < 7; ++i)
        {
            CHECK(queue.TryPush(int{i}) == true);
        }

        // 8th push should fail (queue full).
        CHECK(queue.TryPush(7) == false);

        // Pop all 7 items.
        for (int i = 0; i < 7; ++i)
        {
            auto value = queue.TryPop();
            REQUIRE(value.has_value());
            CHECK(*value == i);
        }

        // Now empty.
        CHECK(queue.TryPop().has_value() == false);
    }

    TEST_CASE("LockFreeMPMCQueue supports multiple producers and consumers without losing or duplicating items")
    {
        using Queue = Limitless::Concurrency::LockFreeMPMCQueue<int, 1024>;
        Queue queue;

        constexpr int kProducers = 4;
        constexpr int kConsumers = 4;
        constexpr int kItemsPerProducer = 5000;
        constexpr int kTotal = kProducers * kItemsPerProducer;

        std::atomic<int> nextId{0};
        std::atomic<int> pushed{0};
        std::atomic<int> popped{0};

        std::vector<std::atomic<int>> seen(static_cast<size_t>(kTotal));
        for (auto& v : seen) v.store(0, std::memory_order_relaxed);

        std::vector<std::thread> producers;
        producers.reserve(kProducers);
        for (int p = 0; p < kProducers; ++p)
        {
            producers.emplace_back([&]()
            {
                for (int i = 0; i < kItemsPerProducer; ++i)
                {
                    const int id = nextId.fetch_add(1, std::memory_order_relaxed);
                    while (!queue.TryPush(int{id}))
                    {
                        std::this_thread::yield();
                    }
                    pushed.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        std::vector<std::thread> consumers;
        consumers.reserve(kConsumers);
        for (int c = 0; c < kConsumers; ++c)
        {
            consumers.emplace_back([&]()
            {
                while (popped.load(std::memory_order_relaxed) < kTotal)
                {
                    auto v = queue.TryPop();
                    if (!v)
                    {
                        std::this_thread::yield();
                        continue;
                    }

                    const int id = *v;
                    if (id >= 0 && id < kTotal)
                    {
                        seen[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed);
                    }
                    popped.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : producers) t.join();
        for (auto& t : consumers) t.join();

        CHECK(pushed.load() == kTotal);
        CHECK(popped.load() == kTotal);

        for (int id = 0; id < kTotal; ++id)
        {
            CHECK(seen[static_cast<size_t>(id)].load(std::memory_order_relaxed) == 1);
        }
    }

    TEST_CASE("LockFreeMPMCQueue bounded overflow returns false")
    {
        using Queue = Limitless::Concurrency::LockFreeMPMCQueue<int, 8>;
        Queue queue;

        int successCount = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (queue.TryPush(int{i}))
            {
                successCount++;
            }
            else
            {
                break;
            }
        }

        CHECK(successCount == 8);
        CHECK(queue.IsFull() == true);
    }

    TEST_CASE("LockFreeMPMCQueue sustained contention stress preserves uniqueness")
    {
        using Queue = Limitless::Concurrency::LockFreeMPMCQueue<int, 4096>;
        Queue queue;

        constexpr int kProducers = 8;
        constexpr int kConsumers = 8;
        constexpr int kItemsPerProducer = 25000;
        constexpr int kTotal = kProducers * kItemsPerProducer;

        std::atomic<int> nextId{0};
        std::atomic<int> popped{0};
        std::vector<std::atomic<int>> seen(static_cast<size_t>(kTotal));
        for (auto& counter : seen)
            counter.store(0, std::memory_order_relaxed);

        std::vector<std::thread> producers;
        producers.reserve(kProducers);
        for (int producer = 0; producer < kProducers; ++producer)
        {
            producers.emplace_back([&]()
            {
                for (int i = 0; i < kItemsPerProducer; ++i)
                {
                    const int id = nextId.fetch_add(1, std::memory_order_relaxed);
                    while (!queue.TryPush(int{id}))
                        std::this_thread::yield();
                }
            });
        }

        std::vector<std::thread> consumers;
        consumers.reserve(kConsumers);
        for (int consumer = 0; consumer < kConsumers; ++consumer)
        {
            consumers.emplace_back([&]()
            {
                while (popped.load(std::memory_order_relaxed) < kTotal)
                {
                    auto value = queue.TryPop();
                    if (!value)
                    {
                        std::this_thread::yield();
                        continue;
                    }

                    const int id = *value;
                    if (id >= 0 && id < kTotal)
                        seen[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed);
                    popped.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& producer : producers)
            producer.join();
        for (auto& consumer : consumers)
            consumer.join();

        CHECK(nextId.load(std::memory_order_relaxed) == kTotal);
        CHECK(popped.load(std::memory_order_relaxed) == kTotal);
        for (int id = 0; id < kTotal; ++id)
        {
            CHECK(seen[static_cast<size_t>(id)].load(std::memory_order_relaxed) == 1);
        }
    }

    TEST_CASE("WorkStealingQueue pop on empty and bounded TryPush are safe")
    {
        using Queue = Limitless::Concurrency::WorkStealingQueue<int, 8>;
        Queue queue;

        CHECK(queue.Pop().has_value() == false);
        CHECK(queue.Steal().has_value() == false);

        for (int i = 0; i < 8; ++i)
        {
            CHECK(queue.TryPush(int{i}) == true);
        }

        CHECK(queue.TryPush(8) == false);
        CHECK(queue.GetSize() == 8);

        // Owner pops from bottom (LIFO).
        auto tail = queue.Pop();
        REQUIRE(tail.has_value());
        CHECK(*tail == 7);

        // Thief steals from top (oldest remaining).
        auto head = queue.Steal();
        REQUIRE(head.has_value());
        CHECK(*head == 0);
    }

    TEST_CASE("WorkStealingQueue Push reports overflow and never silently drops")
    {
        using Queue = Limitless::Concurrency::WorkStealingQueue<int, 4>;
        Queue queue;

        CHECK(queue.Push(1) == true);
        CHECK(queue.Push(2) == true);
        CHECK(queue.Push(3) == true);
        CHECK(queue.Push(4) == true);
        CHECK(queue.Push(5) == false);
    }

    TEST_CASE("WorkStealingQueue owner and thief race never duplicates last-item handoff")
    {
        using Queue = Limitless::Concurrency::WorkStealingQueue<int, 1024>;
        Queue queue;

        constexpr int kIterations = 100000;
        std::vector<std::atomic<int>> seen(static_cast<size_t>(kIterations));
        for (auto& counter : seen)
            counter.store(0, std::memory_order_relaxed);

        std::atomic<int> ownerPops{0};
        std::atomic<int> thiefSteals{0};
        std::atomic<bool> done{false};

        std::thread thief([&]()
        {
            while (!done.load(std::memory_order_acquire) || !queue.IsEmpty())
            {
                auto value = queue.Steal();
                if (!value)
                {
                    std::this_thread::yield();
                    continue;
                }

                const int id = *value;
                if (id >= 0 && id < kIterations)
                    seen[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed);
                thiefSteals.fetch_add(1, std::memory_order_relaxed);
            }
        });

        for (int id = 0; id < kIterations; ++id)
        {
            while (!queue.TryPush(int{id}))
            {
                auto drained = queue.Pop();
                if (drained)
                {
                    const int drainedId = *drained;
                    if (drainedId >= 0 && drainedId < kIterations)
                        seen[static_cast<size_t>(drainedId)].fetch_add(1, std::memory_order_relaxed);
                    ownerPops.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    std::this_thread::yield();
                }
            }

            auto ownerValue = queue.Pop();
            if (ownerValue)
            {
                const int ownerId = *ownerValue;
                if (ownerId >= 0 && ownerId < kIterations)
                    seen[static_cast<size_t>(ownerId)].fetch_add(1, std::memory_order_relaxed);
                ownerPops.fetch_add(1, std::memory_order_relaxed);
            }
        }

        done.store(true, std::memory_order_release);
        thief.join();

        while (auto tail = queue.Pop())
        {
            const int id = *tail;
            if (id >= 0 && id < kIterations)
                seen[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed);
            ownerPops.fetch_add(1, std::memory_order_relaxed);
        }

        const int totalProcessed = ownerPops.load(std::memory_order_relaxed) +
                                   thiefSteals.load(std::memory_order_relaxed);
        CHECK(totalProcessed == kIterations);
        for (int id = 0; id < kIterations; ++id)
        {
            CHECK(seen[static_cast<size_t>(id)].load(std::memory_order_relaxed) == 1);
        }
    }

    TEST_CASE("ObjectPool supports concurrent acquire and release without corruption")
    {
        struct PooledPayload
        {
            uint64_t stamp = 0;
            int ownerThread = -1;
        };

        using Pool = Limitless::Concurrency::ObjectPool<PooledPayload, 256>;
        Pool pool;

        constexpr int kThreads = 8;
        constexpr int kIterationsPerThread = 50000;

        std::atomic<int> invalidObservedState{0};
        std::atomic<uint64_t> stampGenerator{1};

        std::vector<std::thread> workers;
        workers.reserve(kThreads);
        for (int threadIndex = 0; threadIndex < kThreads; ++threadIndex)
        {
            workers.emplace_back([&, threadIndex]()
            {
                for (int i = 0; i < kIterationsPerThread; ++i)
                {
                    auto object = pool.Acquire();
                    if (!object)
                    {
                        invalidObservedState.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    if (object->stamp != 0)
                    {
                        if (object->ownerThread < 0 || object->ownerThread >= kThreads)
                            invalidObservedState.fetch_add(1, std::memory_order_relaxed);
                    }

                    object->ownerThread = threadIndex;
                    object->stamp = stampGenerator.fetch_add(1, std::memory_order_relaxed);
                    pool.Release(std::move(object));
                }
            });
        }

        for (auto& worker : workers)
            worker.join();

        CHECK(invalidObservedState.load(std::memory_order_relaxed) == 0);
    }

    TEST_CASE("LockFreeSPSCQueue Clear resets to empty state")
    {
        using Queue = Limitless::Concurrency::LockFreeSPSCQueue<int, 8>;
        Queue queue;

        for (int i = 0; i < 5; ++i)
            CHECK(queue.TryPush(int{i}));

        CHECK(queue.GetSize() == 5);
        CHECK_FALSE(queue.IsEmpty());

        queue.Clear();

        CHECK(queue.IsEmpty());
        CHECK(queue.GetSize() == 0);
        CHECK_FALSE(queue.IsFull());

        CHECK(queue.TryPush(int{42}));
        auto val = queue.TryPop();
        REQUIRE(val.has_value());
        CHECK(*val == 42);
    }

    TEST_CASE("LockFreeSPSCQueue GetSize tracks push and pop")
    {
        using Queue = Limitless::Concurrency::LockFreeSPSCQueue<int, 16>;
        Queue queue;

        CHECK(queue.GetSize() == 0);
        CHECK(queue.IsEmpty());
        CHECK_FALSE(queue.IsFull());

        queue.TryPush(int{1});
        CHECK(queue.GetSize() == 1);
        CHECK_FALSE(queue.IsEmpty());

        queue.TryPush(int{2});
        queue.TryPush(int{3});
        CHECK(queue.GetSize() == 3);

        queue.TryPop();
        CHECK(queue.GetSize() == 2);

        queue.TryPop();
        queue.TryPop();
        CHECK(queue.GetSize() == 0);
        CHECK(queue.IsEmpty());
    }

    TEST_CASE("LockFreeMPMCQueue Clear resets to empty state")
    {
        using Queue = Limitless::Concurrency::LockFreeMPMCQueue<int, 8>;
        Queue queue;

        for (int i = 0; i < 6; ++i)
            CHECK(queue.TryPush(int{i}));

        CHECK(queue.GetSize() == 6);
        CHECK_FALSE(queue.IsEmpty());

        queue.Clear();

        CHECK(queue.IsEmpty());
        CHECK(queue.GetSize() == 0);
        CHECK_FALSE(queue.IsFull());

        for (int i = 0; i < 8; ++i)
            CHECK(queue.TryPush(int{i}));

        CHECK(queue.IsFull());

        for (int i = 0; i < 8; ++i)
        {
            auto val = queue.TryPop();
            REQUIRE(val.has_value());
            CHECK(*val == i);
        }
    }

    TEST_CASE("LockFreeMPMCQueue GetSize tracks push and pop")
    {
        using Queue = Limitless::Concurrency::LockFreeMPMCQueue<int, 16>;
        Queue queue;

        CHECK(queue.GetSize() == 0);
        CHECK(queue.IsEmpty());

        queue.TryPush(int{10});
        queue.TryPush(int{20});
        CHECK(queue.GetSize() == 2);

        queue.TryPop();
        CHECK(queue.GetSize() == 1);

        queue.TryPop();
        CHECK(queue.GetSize() == 0);
        CHECK(queue.IsEmpty());
    }

    TEST_CASE("WorkStealingQueue Clear via drain leaves queue reusable")
    {
        using Queue = Limitless::Concurrency::WorkStealingQueue<int, 8>;
        Queue queue;

        for (int i = 0; i < 6; ++i)
            CHECK(queue.TryPush(int{i}));

        CHECK(queue.GetSize() == 6);

        while (queue.Pop().has_value()) {}
        CHECK(queue.IsEmpty());
        CHECK(queue.GetSize() == 0);

        CHECK(queue.TryPush(int{99}));
        auto val = queue.Pop();
        REQUIRE(val.has_value());
        CHECK(*val == 99);
    }

    TEST_CASE("ObjectPool Acquire returns valid objects and Release recycles them")
    {
        using Pool = Limitless::Concurrency::ObjectPool<int, 4>;
        Pool pool;

        auto obj1 = pool.Acquire();
        REQUIRE(obj1 != nullptr);
        *obj1 = 42;
        pool.Release(std::move(obj1));

        auto obj2 = pool.Acquire();
        REQUIRE(obj2 != nullptr);
        CHECK(*obj2 == 42);

        pool.Clear();
    }

    TEST_CASE("JobSystem Submit rejects when unavailable and drains all accepted jobs")
    {
        auto& jobSystem = Limitless::Concurrency::GetJobSystem();
        jobSystem.Shutdown();

        CHECK(jobSystem.Submit([]() {}) == false);
        CHECK(jobSystem.TrySubmit([]() {}) == false);

        jobSystem.Initialize(2);

        constexpr int kProducerCount = 4;
        constexpr int kJobsPerProducer = 2000;
        constexpr int kTotalJobs = kProducerCount * kJobsPerProducer;
        std::atomic<int> executed{ 0 };
        std::atomic<int> accepted{ 0 };

        std::vector<std::thread> producers;
        producers.reserve(kProducerCount);
        for (int producer = 0; producer < kProducerCount; ++producer)
        {
            producers.emplace_back([&]() {
                for (int i = 0; i < kJobsPerProducer; ++i)
                {
                    const bool submitted = jobSystem.Submit([&executed]() {
                        executed.fetch_add(1, std::memory_order_relaxed);
                    });
                    if (submitted)
                        accepted.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& producer : producers)
            producer.join();

        jobSystem.Wait();

        CHECK(accepted.load(std::memory_order_relaxed) == kTotalJobs);
        CHECK(executed.load(std::memory_order_relaxed) == kTotalJobs);

        jobSystem.Shutdown();
    }

    TEST_CASE("WaitGroup blocks until all work is done")
    {
        Limitless::Concurrency::WaitGroup wg;

        constexpr int kCount = 8;
        std::atomic<int> completed{0};

        std::vector<std::thread> workers;
        workers.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
        {
            wg.Add(1);
            workers.emplace_back([&wg, &completed]()
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                completed.fetch_add(1, std::memory_order_relaxed);
                wg.Done();
            });
        }

        wg.Wait();
        CHECK(completed.load(std::memory_order_relaxed) == kCount);

        for (auto& worker : workers)
            worker.join();
    }

    TEST_CASE("JobSystem ParallelFor processes all items")
    {
        auto& jobSystem = Limitless::Concurrency::GetJobSystem();
        jobSystem.Shutdown();
        jobSystem.Initialize(4);

        constexpr size_t kSize = 10000;
        std::vector<std::atomic<int>> results(kSize);
        for (auto& r : results)
            r.store(0, std::memory_order_relaxed);

        jobSystem.ParallelFor(0, kSize, 0, [&results](size_t index)
        {
            results[index].fetch_add(1, std::memory_order_relaxed);
        });

        for (size_t i = 0; i < kSize; ++i)
        {
            CHECK(results[i].load(std::memory_order_relaxed) == 1);
        }

        jobSystem.Shutdown();
    }

    TEST_CASE("JobSystem ParallelFor empty range is a no-op")
    {
        auto& jobSystem = Limitless::Concurrency::GetJobSystem();
        jobSystem.Shutdown();
        jobSystem.Initialize(2);

        std::atomic<int> executed{0};
        jobSystem.ParallelFor(5, 5, 1, [&executed](size_t)
        {
            executed.fetch_add(1, std::memory_order_relaxed);
        });

        CHECK(executed.load(std::memory_order_relaxed) == 0);

        jobSystem.ParallelFor(10, 3, 1, [&executed](size_t)
        {
            executed.fetch_add(1, std::memory_order_relaxed);
        });

        CHECK(executed.load(std::memory_order_relaxed) == 0);

        jobSystem.Shutdown();
    }

    TEST_CASE("JobSystem ParallelFor falls back to sequential when not initialized")
    {
        auto& jobSystem = Limitless::Concurrency::GetJobSystem();
        jobSystem.Shutdown();
        CHECK_FALSE(jobSystem.IsInitialized());

        constexpr size_t kSize = 100;
        std::vector<int> results(kSize, 0);

        jobSystem.ParallelFor(0, kSize, 10, [&results](size_t index)
        {
            results[index] = static_cast<int>(index) + 1;
        });

        for (size_t i = 0; i < kSize; ++i)
        {
            CHECK(results[i] == static_cast<int>(i) + 1);
        }
    }
}

