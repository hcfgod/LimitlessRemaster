#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Core/Concurrency/JobSystem.h"
#include "Core/Concurrency/LockFreeQueue.h"

#include <atomic>
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
}

