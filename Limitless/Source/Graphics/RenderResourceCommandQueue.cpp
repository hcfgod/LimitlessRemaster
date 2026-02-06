#include "RenderResourceCommandQueue.h"
#include "Graphics/GraphicsContext.h"
#include "Core/Debug/Log.h"

namespace Limitless
{
    bool RenderResourceCommandQueue::Submit(std::unique_ptr<Command> command)
    {
        if (!command)
        {
            return false;
        }

        uint32_t size = m_ApproxSize.load(std::memory_order_relaxed);
        for (;;)
        {
            if (size >= kQueueCapacity)
            {
                LT_CORE_WARN("RenderResourceCommandQueue is full, resource command dropped");
                return false;
            }

            if (m_ApproxSize.compare_exchange_weak(
                    size, size + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                break;
            }
        }

        QueuedCommand queued{};
        queued.command = std::move(command);
        if (m_Queue.TryPush(std::move(queued)))
        {
            return true;
        }

        m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
        LT_CORE_WARN("RenderResourceCommandQueue push failed, command dropped");
        return false;
    }

    void RenderResourceCommandQueue::Process(GraphicsContext* context, uint32_t maxCommands)
    {
        if (!context)
        {
            return;
        }

        uint32_t processed = 0;
        while (processed < maxCommands)
        {
            auto item = m_Queue.TryPop();
            if (!item)
            {
                break;
            }

            m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);

            if (item->command)
            {
                item->command->Execute(context);
            }

            processed++;
        }
    }
}

