#include <doctest/doctest.h>

#include "Graphics/GraphicsContext.h"
#include "Graphics/OpenGL/OpenGLRenderCommandTestHooks.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/RenderCommandQueue.h"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
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
        void SetViewport(int, int, int, int) override {}
        Limitless::GraphicsAPI GetAPI() const override { return Limitless::GraphicsAPI::OpenGL; }
        bool SetVSync(bool) override { return true; }
        bool IsVSync() const override { return false; }
    };
}

TEST_SUITE("Correctness Contracts - Render Queue Regressions")
{
    TEST_CASE("Priority sorting preserves FIFO order within the same priority band")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 64;
        config.maxCommandsPerFrame = 64;
        config.enableBatching = false;
        config.enablePrioritySorting = true;
        config.enableStatistics = true;

        Limitless::RenderCommandQueue queue(config);
        NullGraphicsContext context;

        std::mutex executionMutex;
        std::vector<std::string> executionOrder;
        executionOrder.reserve(4);

        auto makeCommand = [&](std::string name) {
            return std::make_unique<Limitless::CustomCommand>(
                [&, name = std::move(name)](Limitless::GraphicsContext*) {
                    std::lock_guard<std::mutex> lock(executionMutex);
                    executionOrder.push_back(name);
                },
                "PriorityOrderCommand");
        };

        REQUIRE(queue.SubmitCommandWithPriority(makeCommand("normal-0"), Limitless::RenderCommandPriority::Normal));
        REQUIRE(queue.SubmitCommandWithPriority(makeCommand("high-0"), Limitless::RenderCommandPriority::High));
        REQUIRE(queue.SubmitCommandWithPriority(makeCommand("high-1"), Limitless::RenderCommandPriority::High));
        REQUIRE(queue.SubmitCommandWithPriority(makeCommand("normal-1"), Limitless::RenderCommandPriority::Normal));

        queue.ProcessCommands(&context);

        REQUIRE(executionOrder.size() == 4);
        CHECK(executionOrder[0] == "high-0");
        CHECK(executionOrder[1] == "high-1");
        CHECK(executionOrder[2] == "normal-0");
        CHECK(executionOrder[3] == "normal-1");

        const auto stats = queue.GetStats();
        CHECK(stats.totalCommandsSubmitted == 4);
        CHECK(stats.totalCommandsExecuted == 4);
        CHECK(stats.totalCommandsDropped == 0);
    }

    TEST_CASE("Batching preserves submission order for state-sensitive command sequences")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 64;
        config.maxCommandsPerFrame = 64;
        config.enableBatching = true;
        config.enablePrioritySorting = false;
        config.enableStatistics = false;

        Limitless::RenderCommandQueue queue(config);
        NullGraphicsContext context;

        std::mutex executionMutex;
        std::vector<std::string> executionOrder;
        executionOrder.reserve(5);

        const std::vector<std::string> expectedOrder = {
            "bind-framebuffer",
            "clear-target",
            "draw-world",
            "custom-barrier",
            "draw-overlay"
        };

        for (const std::string& name : expectedOrder)
        {
            REQUIRE(queue.SubmitCommand(std::make_unique<Limitless::CustomCommand>(
                [&, name](Limitless::GraphicsContext*) {
                    std::lock_guard<std::mutex> lock(executionMutex);
                    executionOrder.push_back(name);
                },
                name)));
        }

        queue.ProcessCommands(&context);

        REQUIRE(executionOrder.size() == expectedOrder.size());
        for (size_t index = 0; index < expectedOrder.size(); ++index)
            CHECK(executionOrder[index] == expectedOrder[index]);
    }

    TEST_CASE("ProcessCommands respects maxCommandsPerFrame budget and leaves remainder queued")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 64;
        config.maxCommandsPerFrame = 3;
        config.enableBatching = true;
        config.enablePrioritySorting = false;
        config.enableStatistics = true;

        Limitless::RenderCommandQueue queue(config);
        NullGraphicsContext context;

        std::atomic<int> executed{ 0 };
        for (int index = 0; index < 7; ++index)
        {
            REQUIRE(queue.SubmitCommand(std::make_unique<Limitless::CustomCommand>(
                [&](Limitless::GraphicsContext*) {
                    executed.fetch_add(1, std::memory_order_relaxed);
                },
                "FrameBudgetCommand")));
        }

        queue.ProcessCommands(&context);
        CHECK(executed.load(std::memory_order_relaxed) == 3);
        CHECK(queue.GetSize() == 4);
        CHECK(queue.IsEmpty() == false);

        queue.ProcessCommands(&context);
        CHECK(executed.load(std::memory_order_relaxed) == 6);
        CHECK(queue.GetSize() == 1);

        queue.ProcessCommands(&context);
        CHECK(executed.load(std::memory_order_relaxed) == 7);
        CHECK(queue.GetSize() == 0);
        CHECK(queue.IsEmpty() == true);

        const auto stats = queue.GetStats();
        CHECK(stats.totalCommandsSubmitted == 7);
        CHECK(stats.totalCommandsExecuted == 7);
        CHECK(stats.totalCommandsDropped == 0);
    }

    TEST_CASE("Flush waits for in-flight execution after queue pop")
    {
        Limitless::RenderQueueConfig config;
        config.maxQueueSize = 8;
        config.maxCommandsPerFrame = 8;
        config.enableBatching = false;
        config.enablePrioritySorting = false;
        config.enableStatistics = false;

        Limitless::RenderCommandQueue queue(config);
        NullGraphicsContext context;

        std::promise<void> commandStartedPromise;
        auto commandStartedFuture = commandStartedPromise.get_future();
        std::promise<void> releaseCommandPromise;
        auto releaseCommandFuture = releaseCommandPromise.get_future();
        std::atomic<bool> flushReturned{ false };

        REQUIRE(queue.SubmitCommand(std::make_unique<Limitless::CustomCommand>(
            [&](Limitless::GraphicsContext*) {
                commandStartedPromise.set_value();
                releaseCommandFuture.wait();
            },
            "BlockingFlushCommand")));

        std::thread flushThread([&]() {
            queue.Flush();
            flushReturned.store(true, std::memory_order_release);
        });

        std::thread processThread([&]() {
            queue.ProcessCommandsBatch(&context, 1);
        });

        commandStartedFuture.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(flushReturned.load(std::memory_order_acquire) == false);

        releaseCommandPromise.set_value();

        processThread.join();
        flushThread.join();

        CHECK(flushReturned.load(std::memory_order_acquire) == true);
        CHECK(queue.IsEmpty() == true);
        CHECK(queue.GetSize() == 0);
    }

    TEST_CASE("CustomCommand invalidates cached OpenGL state and uniform caches")
    {
        NullGraphicsContext context;

        Limitless::OpenGLRenderCommandTestHooks::ResetRuntimeState();

        Limitless::OpenGLRenderCommandTestHooks::RuntimeStateSnapshot seededState{};
        seededState.Program = 17;
        seededState.VertexArray = 23;
        seededState.ActiveTextureUnit = 5;
        seededState.BoundTexture2D[0] = 41;
        seededState.BoundTexture2D[1] = 42;
        seededState.BoundTexture2D[5] = 99;
        seededState.Renderer2DProgram = 17;
        seededState.ViewProjectionLocation = 7;
        seededState.ModelLocation = 11;
        seededState.HasViewProjection = true;
        seededState.UniformProgramCount = 1;
        seededState.UniformLocationCount = 3;
        Limitless::OpenGLRenderCommandTestHooks::SetRuntimeStateSnapshot(seededState);

        bool customFunctionRan = false;
        Limitless::CustomCommand command(
            [&](Limitless::GraphicsContext*) {
                customFunctionRan = true;
            },
            "InvalidateStateCache");

        command.Execute(&context);

        CHECK(customFunctionRan == true);

        const auto clearedState = Limitless::OpenGLRenderCommandTestHooks::GetRuntimeStateSnapshot();
        CHECK(clearedState.Program == 0u);
        CHECK(clearedState.VertexArray == 0u);
        CHECK(clearedState.ActiveTextureUnit == 0u);
        CHECK(clearedState.BoundTexture2D[0] == 0u);
        CHECK(clearedState.BoundTexture2D[1] == 0u);
        CHECK(clearedState.BoundTexture2D[5] == 0u);
        CHECK(clearedState.Renderer2DProgram == 0u);
        CHECK(clearedState.ViewProjectionLocation == -2);
        CHECK(clearedState.ModelLocation == -2);
        CHECK(clearedState.HasViewProjection == false);
        CHECK(clearedState.UniformProgramCount == 0u);
        CHECK(clearedState.UniformLocationCount == 0u);

        Limitless::OpenGLRenderCommandTestHooks::ResetRuntimeState();
    }
}
