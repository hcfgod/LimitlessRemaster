#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Core/EventSystem.h"

#include <atomic>
#include <thread>
#include <vector>

TEST_SUITE("Correctness Contracts - Shutdown Races")
{
    TEST_CASE("EventSystem::Shutdown is race-safe with concurrent DispatchDeferred/ProcessEvents and stops callbacks after returning")
    {
        auto& events = Limitless::GetEventSystem();
        events.Initialize();

        std::atomic<int> handled{0};
        events.AddCallback(Limitless::EventType::AppTick, [&](Limitless::Event&)
        {
            handled.fetch_add(1, std::memory_order_relaxed);
        });

        std::atomic<bool> keepRunning{true};

        // Producer threads: keep enqueueing deferred events.
        constexpr int kProducerThreads = 4;
        std::vector<std::thread> producers;
        producers.reserve(kProducerThreads);
        for (int i = 0; i < kProducerThreads; ++i)
        {
            producers.emplace_back([&]()
            {
                while (keepRunning.load(std::memory_order_relaxed))
                {
                    auto e = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
                    events.DispatchDeferred(std::move(e));
                    std::this_thread::yield();
                }
            });
        }

        // Consumer thread: process events.
        std::thread processor([&]()
        {
            while (keepRunning.load(std::memory_order_relaxed))
            {
                events.ProcessEvents(64);
                std::this_thread::yield();
            }
        });

        // Force some activity before shutdown.
        for (int i = 0; i < 5000; ++i)
        {
            if (handled.load(std::memory_order_relaxed) > 0)
                break;
            std::this_thread::yield();
        }

        // Shutdown while other threads are actively dispatching/processing.
        CHECK_NOTHROW(events.Shutdown());

        const int afterShutdown = handled.load(std::memory_order_relaxed);

        // Even if other threads continue calling into the API briefly, callbacks must not execute after Shutdown returns.
        for (int i = 0; i < 1000; ++i)
        {
            Limitless::Events::AppTickEvent e(0.016f);
            events.DispatchImmediate(e);
            events.ProcessEvents();
        }

        CHECK(handled.load(std::memory_order_relaxed) == afterShutdown);

        // Stop worker threads and join.
        keepRunning.store(false, std::memory_order_relaxed);
        for (auto& t : producers) t.join();
        processor.join();
    }
}

