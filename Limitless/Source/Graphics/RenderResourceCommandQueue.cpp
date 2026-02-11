#include "RenderResourceCommandQueue.h"
#include "Graphics/GraphicsContext.h"
#include "Core/Debug/Log.h"

#include <algorithm>

namespace Limitless
{
    RenderResourceCommandQueue::RenderResourceCommandQueue()
    {
        // Reserve the last slot as a stable "catch-all" bucket.
        m_DebugLabelNames[kMaxDebugLabels - 1] = "OtherResourceCommands";
        m_DebugLabelSubmitted[kMaxDebugLabels - 1].store(0, std::memory_order_relaxed);
        m_DebugLabelProcessed[kMaxDebugLabels - 1].store(0, std::memory_order_relaxed);
    }

    uint32_t RenderResourceCommandQueue::FindOrAddDebugLabelIndex(const char* name)
    {
        const char* safeName = (name && name[0] != '\0') ? name : "UnnamedResourceCommand";

        const uint32_t count = m_DebugLabelCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i)
        {
            const char* existing = m_DebugLabelNames[i];
            if (existing == safeName)
            {
                return i;
            }
            if (existing && std::strcmp(existing, safeName) == 0)
            {
                return i;
            }
        }

        std::lock_guard<std::mutex> lock(m_DebugLabelMutex);

        // Re-check under the lock.
        uint32_t lockedCount = m_DebugLabelCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < lockedCount; ++i)
        {
            const char* existing = m_DebugLabelNames[i];
            if (existing == safeName)
            {
                return i;
            }
            if (existing && std::strcmp(existing, safeName) == 0)
            {
                return i;
            }
        }

        if (lockedCount >= kMaxDebugLabels)
        {
            return kMaxDebugLabels - 1; // Clamp to last slot.
        }

        // Keep the last slot reserved for "OtherResourceCommands".
        if (lockedCount == (kMaxDebugLabels - 1))
        {
            return kMaxDebugLabels - 1;
        }

        const uint32_t newIndex = lockedCount;
        m_DebugLabelNames[newIndex] = safeName;
        m_DebugLabelSubmitted[newIndex].store(0, std::memory_order_relaxed);
        m_DebugLabelProcessed[newIndex].store(0, std::memory_order_relaxed);
        m_DebugLabelCount.store(lockedCount + 1, std::memory_order_release);
        return newIndex;
    }

    RenderResourceCommandQueue::DebugLabelSnapshot RenderResourceCommandQueue::GetDebugLabelSnapshot() const
    {
        DebugLabelSnapshot s{};
        s.Count = std::min<uint32_t>(m_DebugLabelCount.load(std::memory_order_acquire), kMaxDebugLabels);
        for (uint32_t i = 0; i < s.Count; ++i)
        {
            s.Entries[i].Name = m_DebugLabelNames[i];
            s.Entries[i].Submitted = m_DebugLabelSubmitted[i].load(std::memory_order_relaxed);
            s.Entries[i].Processed = m_DebugLabelProcessed[i].load(std::memory_order_relaxed);
        }
        return s;
    }

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
        const uint32_t labelIndex = FindOrAddDebugLabelIndex(command->GetDebugName());
        queued.command = std::move(command);
        if (m_Queue.TryPush(std::move(queued)))
        {
            m_TotalSubmitted.fetch_add(1, std::memory_order_relaxed);
            m_DebugLabelSubmitted[labelIndex].fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
        LT_CORE_WARN("RenderResourceCommandQueue push failed, command dropped");
        return false;
    }

    uint32_t RenderResourceCommandQueue::Process(GraphicsContext* context, uint32_t maxCommands)
    {
        if (!context)
        {
            return 0;
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
                const uint32_t labelIndex = FindOrAddDebugLabelIndex(item->command->GetDebugName());
                item->command->Execute(context);
                m_DebugLabelProcessed[labelIndex].fetch_add(1, std::memory_order_relaxed);
            }

            processed++;
        }

        if (processed > 0)
        {
            m_TotalProcessed.fetch_add(processed, std::memory_order_relaxed);
        }

        return processed;
    }
}

