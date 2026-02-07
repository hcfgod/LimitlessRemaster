#pragma once

#include <atomic>
#include <cstdint>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetLoadCoordinator
    // Generation-based cancellation for in-flight async loads.
    //
    // Usage:
    // - Each async load captures the current generation at start.
    // - Before completing (and ideally before expensive GPU work), it checks whether
    //   generation still matches. If not, it drops the result (returns nullptr).
    //
    // Scene-change integration:
    // - Call CancelAllInFlightLoads() when switching scenes or rebuilding gameplay state.
    // -----------------------------------------------------------------------------
    class AssetLoadCoordinator final
    {
    public:
        static uint64_t GetGeneration()
        {
            return s_Generation.load(std::memory_order_relaxed);
        }

        static uint64_t CancelAllInFlightLoads()
        {
            return s_Generation.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        static bool IsGenerationCurrent(uint64_t generation)
        {
            return generation == GetGeneration();
        }

    private:
        static std::atomic<uint64_t> s_Generation;
    };
}

