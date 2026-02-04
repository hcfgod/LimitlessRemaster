#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Core/EventSystem.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/RenderCommandQueue.h"

#include <atomic>
#include <thread>
#include <vector>

namespace
{
    class NullGraphicsContext final : public Limitless::GraphicsContext
    {
    public:
        void SetupAttributes() override {}
        void MakeCurrent() override {}
        void Init(void*, Limitless::GraphicsAPI) override {}
        void SwapBuffers() override {}
        bool SetVSync(bool) override { return true; }
        bool IsVSync() const override { return false; }
    };
}

TEST_SUITE("Correctness Contracts - Event and Render Submission")
{
    TEST_CASE("EventSystem deferred dispatch is safe for multi-thread producers; processing is single-thread")
    {
        auto& events = Limitless::GetEventSystem();
        events.Initialize();

        std::atomic<int> handled{0};
        events.AddCallback(Limitless::EventType::AppTick, [&](Limitless::Event&)
        {
            handled.fetch_add(1, std::memory_order_relaxed);
        });

        constexpr int kThreads = 4;
        constexpr int kPerThread = 250;
        constexpr int kTotal = kThreads * kPerThread;

        std::vector<std::thread> producers;
        producers.reserve(kThreads);
        for (int i = 0; i < kThreads; ++i)
        {
            producers.emplace_back([&]()
            {
                for (int n = 0; n < kPerThread; ++n)
                {
                    auto e = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
                    events.DispatchDeferred(std::move(e));
                }
            });
        }

        for (auto& t : producers) t.join();

        // Single-thread processing step: drain the queue.
        events.ProcessEvents();

        CHECK(handled.load(std::memory_order_relaxed) == kTotal);

        events.Shutdown();
    }

    TEST_CASE("EventSystem calls after Shutdown are safe no-ops")
    {
        auto& events = Limitless::GetEventSystem();
        events.Initialize();
        events.Shutdown();

        // Should not crash (contract: safe no-op with warnings).
        Limitless::Events::AppRenderEvent e;
        CHECK_NOTHROW(events.Dispatch(e));
        CHECK_NOTHROW(events.ProcessEvents());
        CHECK_NOTHROW(events.AddCallback(Limitless::EventType::AppTick, [](Limitless::Event&) {}));
    }

    TEST_CASE("RenderCommandQueue supports multi-thread submission and single-thread execution with a non-null GraphicsContext")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 1024; // bounded contract (<= fixed capacity)
        config.maxCommandsPerFrame = 100000;
        config.enableBatching = false;
        config.enablePrioritySorting = false;
        config.enableStatistics = false;

        Limitless::RenderCommandQueue queue(config);
        NullGraphicsContext context;

        constexpr int kThreads = 4;
        constexpr int kPerThread = 200;
        constexpr int kTotal = kThreads * kPerThread;

        std::atomic<int> executed{0};
        std::atomic<bool> submissionFailed{false};

        std::vector<std::thread> producers;
        producers.reserve(kThreads);
        for (int i = 0; i < kThreads; ++i)
        {
            producers.emplace_back([&]()
            {
                for (int n = 0; n < kPerThread; ++n)
                {
                    auto cmd = std::make_unique<Limitless::CustomCommand>(
                        [&](Limitless::GraphicsContext* ctx)
                        {
                            // Contract: context passed to queued commands is non-null.
                            CHECK(ctx != nullptr);
                            executed.fetch_add(1, std::memory_order_relaxed);
                        },
                        "ContractTestCommand"
                    );

                    // Under this config, all submits should succeed (1024 cap, 800 submissions).
                    if (!queue.SubmitCommand(std::move(cmd)))
                    {
                        submissionFailed.store(true, std::memory_order_relaxed);
                    }
                }
            });
        }

        for (auto& t : producers) t.join();
        CHECK(submissionFailed.load(std::memory_order_relaxed) == false);

        // Single consumer thread: execute until empty.
        while (!queue.IsEmpty())
        {
            queue.ProcessCommandsBatch(&context, 64);
        }

        CHECK(executed.load(std::memory_order_relaxed) == kTotal);
    }

    TEST_CASE("RenderCommandQueue enforces maxQueueSize bound and drops on overflow")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 64;
        config.maxCommandsPerFrame = 100000;
        config.enableBatching = false;
        config.enablePrioritySorting = false;
        config.enableStatistics = true; // validate stats-driven drop accounting

        Limitless::RenderCommandQueue queue(config);

        std::atomic<int> accepted{0};
        std::atomic<int> dropped{0};

        for (int i = 0; i < 256; ++i)
        {
            auto cmd = std::make_unique<Limitless::CustomCommand>(
                [](Limitless::GraphicsContext*) {},
                "OverflowTestCommand"
            );

            if (queue.SubmitCommand(std::move(cmd)))
                accepted.fetch_add(1, std::memory_order_relaxed);
            else
                dropped.fetch_add(1, std::memory_order_relaxed);
        }

        CHECK(accepted.load(std::memory_order_relaxed) == 64);
        CHECK(dropped.load(std::memory_order_relaxed) == 192);

        // Stats should agree with our counts.
        auto stats = queue.GetStats();
        CHECK(stats.totalCommandsSubmitted == 64);
        CHECK(stats.totalCommandsDropped == 192);
    }
}

