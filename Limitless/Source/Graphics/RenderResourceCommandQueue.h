#pragma once

#include "Core/Concurrency/LockFreeQueue.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <array>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace Limitless
{
    class GraphicsContext;

    /**
     * RenderResourceCommandQueue
     *
     * A dedicated queue for GPU resource operations that must execute on the render thread
     * (or more specifically: on the thread that owns/makes-current the graphics context).
     *
     * - Submission: safe from multiple producer threads.
     * - Execution: intended to run on ONE thread (the render thread) with a valid current context.
     */
    class RenderResourceCommandQueue final
    {
    public:
        class Command
        {
        public:
            virtual ~Command() = default;
            virtual void Execute(GraphicsContext* context) = 0;

            // Optional debug label used for resource telemetry.
            // Prefer passing a string literal with static storage lifetime.
            virtual const char* GetDebugName() const { return "UnnamedResourceCommand"; }
        };

        static constexpr uint32_t kQueueCapacity = 4096;

        RenderResourceCommandQueue();
        ~RenderResourceCommandQueue() = default;

        RenderResourceCommandQueue(const RenderResourceCommandQueue&) = delete;
        RenderResourceCommandQueue& operator=(const RenderResourceCommandQueue&) = delete;

        bool Submit(std::unique_ptr<Command> command);

        // Drain queued resource commands. Must be called on the context-owning thread.
        // Returns the number of commands processed.
        uint32_t Process(GraphicsContext* context, uint32_t maxCommands = 1024);

        // IMPORTANT:
        // Do not rely on the underlying MPMC queue's IsEmpty() as a wake predicate.
        // Some lock-free queue implementations can report "empty" transiently under contention,
        // which is fine for best-effort polling but disastrous for condition-variable waits.
        //
        // We maintain an explicit approximate size counter that is sufficient for:
        // - waking worker threads
        // - avoiding "sleep forever with work queued" deadlocks
        bool IsEmpty() const { return m_ApproxSize.load(std::memory_order_relaxed) == 0; }
        uint32_t GetApproxSize() const { return m_ApproxSize.load(std::memory_order_relaxed); }

        // Monotonic counters (debug/telemetry). Useful for per-frame deltas.
        uint64_t GetTotalSubmitted() const { return m_TotalSubmitted.load(std::memory_order_relaxed); }
        uint64_t GetTotalProcessed() const { return m_TotalProcessed.load(std::memory_order_relaxed); }

        static constexpr uint32_t kMaxDebugLabels = 64;

        struct DebugLabelEntry
        {
            const char* Name = nullptr;
            uint64_t Submitted = 0;
            uint64_t Processed = 0;
        };

        struct DebugLabelSnapshot
        {
            uint32_t Count = 0;
            std::array<DebugLabelEntry, kMaxDebugLabels> Entries{};
        };

        // Thread-safe, allocation-free snapshot of per-label totals.
        DebugLabelSnapshot GetDebugLabelSnapshot() const;

        template<typename Func>
        auto SubmitAndWait(Func&& func) -> decltype(func(static_cast<GraphicsContext*>(nullptr)))
        {
            using ResultT = decltype(func(static_cast<GraphicsContext*>(nullptr)));

            struct SharedState
            {
                std::promise<ResultT> promise;
                std::function<ResultT(GraphicsContext*)> function;
            };

            auto state = std::make_shared<SharedState>();
            state->function = std::forward<Func>(func);
            std::future<ResultT> future = state->promise.get_future();

            class CommandImpl final : public Command
            {
            public:
                explicit CommandImpl(std::shared_ptr<SharedState> shared)
                    : m_Shared(std::move(shared))
                {
                }

                void Execute(GraphicsContext* context) override
                {
                    try
                    {
                        if constexpr (std::is_void_v<ResultT>)
                        {
                            m_Shared->function(context);
                            m_Shared->promise.set_value();
                        }
                        else
                        {
                            m_Shared->promise.set_value(m_Shared->function(context));
                        }
                    }
                    catch (...)
                    {
                        m_Shared->promise.set_exception(std::current_exception());
                    }
                }

            private:
                std::shared_ptr<SharedState> m_Shared;
            };

            if (!Submit(std::make_unique<CommandImpl>(state)))
            {
                throw std::runtime_error("RenderResourceCommandQueue is full");
            }

            if constexpr (std::is_void_v<ResultT>)
            {
                future.get();
            }
            else
            {
                return future.get();
            }
        }

    private:
        struct QueuedCommand
        {
            std::unique_ptr<Command> command;
        };

        Concurrency::LockFreeMPMCQueue<QueuedCommand, kQueueCapacity> m_Queue;
        std::atomic<uint32_t> m_ApproxSize{0};
        std::atomic<uint64_t> m_TotalSubmitted{0};
        std::atomic<uint64_t> m_TotalProcessed{0};

        // Debug label telemetry.
        // Multiple threads may submit and multiple threads may process (render thread + resource thread),
        // so label registration uses a mutex and counters are atomic.
        uint32_t FindOrAddDebugLabelIndex(const char* name);

        mutable std::mutex m_DebugLabelMutex;
        std::atomic<uint32_t> m_DebugLabelCount{0};
        std::array<const char*, kMaxDebugLabels> m_DebugLabelNames{};
        std::array<std::atomic<uint64_t>, kMaxDebugLabels> m_DebugLabelSubmitted{};
        std::array<std::atomic<uint64_t>, kMaxDebugLabels> m_DebugLabelProcessed{};
    };
}

