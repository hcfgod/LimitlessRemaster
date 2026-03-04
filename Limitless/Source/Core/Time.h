#pragma once

#include <cstdint>
#include <chrono>

namespace Limitless
{
    /// Unity-style time API for the engine main thread.
    ///
    /// Design goals:
    /// - Provide a single authoritative source for per-frame delta time.
    /// - Support time scaling (pause / slow-motion) without affecting "real time".
    /// - Provide a fixed-step accumulator for deterministic simulation updates.
    ///
    /// Usage:
    /// - Call Time::Initialize() once during application startup.
    /// - Call Time::Update() exactly once per frame (from the main loop).
    /// - Use Time::GetDeltaTime() for gameplay updates (scaled).
    /// - Use Time::GetUnscaledDeltaTime() for UI/tools and non-gameplay systems.
    class Time final
    {
    public:
        Time() = delete;

        // Lifecycle
        static void Initialize();
        static void Shutdown();

        /// Advance the time system by one frame.
        /// Must be called once per frame from the main thread.
        static void Update();

        // Scaled time (affected by TimeScale)
        static float GetDeltaTimeSeconds() noexcept { return s_DeltaTimeSeconds; }
        static double GetDeltaTimeSecondsDouble() noexcept { return s_DeltaTimeSecondsDouble; }

        // Unscaled time (real frame delta)
        static float GetUnscaledDeltaTimeSeconds() noexcept { return s_UnscaledDeltaTimeSeconds; }
        static double GetUnscaledDeltaTimeSecondsDouble() noexcept { return s_UnscaledDeltaTimeSecondsDouble; }

        // Accumulated time since startup
        static float GetTimeSinceStartupSeconds() noexcept { return s_TimeSinceStartupSeconds; }                 // scaled
        static float GetUnscaledTimeSinceStartupSeconds() noexcept { return s_UnscaledTimeSinceStartupSeconds; } // unscaled

        // Frame counter
        static uint64_t GetFrameCount() noexcept { return s_FrameCount; }

        // Time scaling
        static void SetTimeScale(float timeScale) noexcept;
        static float GetTimeScale() noexcept { return s_TimeScale; }

        // Delta-time clamping (prevents spiral-of-death after stalls / breakpoints)
        static void SetMaximumDeltaTimeSeconds(float maxDeltaTimeSeconds) noexcept;
        static float GetMaximumDeltaTimeSeconds() noexcept { return s_MaximumDeltaTimeSeconds; }

        // Fixed-step support (for deterministic simulation)
        static void SetFixedDeltaTimeSeconds(float fixedDeltaTimeSeconds) noexcept;
        static float GetFixedDeltaTimeSeconds() noexcept { return s_FixedDeltaTimeSeconds; }
        static float GetFixedTimeAccumulatorSeconds() noexcept { return s_FixedTimeAccumulatorSeconds; }

        /// Consume one fixed step if enough unscaled time has accumulated.
        ///
        /// Typical usage:
        ///
        /// while (Time::TryConsumeFixedStep())
        /// {
        ///     // FixedUpdate-style logic here.
        /// }
        static bool TryConsumeFixedStep() noexcept;

        static bool IsInitialized() noexcept { return s_Initialized; }

    private:
        using Clock = std::chrono::steady_clock;

        static Clock::time_point s_StartTime;
        static Clock::time_point s_LastFrameTime;

        static bool s_Initialized;
        static uint64_t s_FrameCount;

        // Per-frame deltas
        static float s_UnscaledDeltaTimeSeconds;
        static double s_UnscaledDeltaTimeSecondsDouble;
        static float s_DeltaTimeSeconds;
        static double s_DeltaTimeSecondsDouble;

        // Accumulated times
        static float s_UnscaledTimeSinceStartupSeconds;
        static float s_TimeSinceStartupSeconds;

        // Scaling and clamping
        static float s_TimeScale;
        static float s_MaximumDeltaTimeSeconds;

        // Fixed step
        static float s_FixedDeltaTimeSeconds;
        static float s_FixedTimeAccumulatorSeconds;
        static uint32_t s_MaximumFixedStepsPerFrame;
        static uint32_t s_FixedStepsConsumedThisFrame;
    };
}

