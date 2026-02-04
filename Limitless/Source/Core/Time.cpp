#include "Core/Time.h"

#include <algorithm>
#include <cmath>

namespace Limitless
{
    Time::Clock::time_point Time::s_StartTime{};
    Time::Clock::time_point Time::s_LastFrameTime{};

    bool Time::s_Initialized = false;
    uint64_t Time::s_FrameCount = 0;

    float Time::s_UnscaledDeltaTimeSeconds = 0.0f;
    double Time::s_UnscaledDeltaTimeSecondsDouble = 0.0;
    float Time::s_DeltaTimeSeconds = 0.0f;
    double Time::s_DeltaTimeSecondsDouble = 0.0;

    float Time::s_UnscaledTimeSinceStartupSeconds = 0.0f;
    float Time::s_TimeSinceStartupSeconds = 0.0f;

    float Time::s_TimeScale = 1.0f;
    float Time::s_MaximumDeltaTimeSeconds = 0.25f; // 250ms clamp by default (debugger / hitch safety)

    float Time::s_FixedDeltaTimeSeconds = 1.0f / 60.0f;
    float Time::s_FixedTimeAccumulatorSeconds = 0.0f;
    uint32_t Time::s_MaximumFixedStepsPerFrame = 8;
    uint32_t Time::s_FixedStepsConsumedThisFrame = 0;

    void Time::Initialize()
    {
        if (s_Initialized)
            return;

        s_StartTime = Clock::now();
        s_LastFrameTime = s_StartTime;

        s_FrameCount = 0;

        s_UnscaledDeltaTimeSeconds = 0.0f;
        s_UnscaledDeltaTimeSecondsDouble = 0.0;
        s_DeltaTimeSeconds = 0.0f;
        s_DeltaTimeSecondsDouble = 0.0;

        s_UnscaledTimeSinceStartupSeconds = 0.0f;
        s_TimeSinceStartupSeconds = 0.0f;

        s_TimeScale = 1.0f;
        s_MaximumDeltaTimeSeconds = 0.25f;

        s_FixedDeltaTimeSeconds = 1.0f / 60.0f;
        s_FixedTimeAccumulatorSeconds = 0.0f;
        s_MaximumFixedStepsPerFrame = 8;

        s_Initialized = true;
    }

    void Time::Shutdown()
    {
        // Keep shutdown lightweight; Time is pure static state.
        s_Initialized = false;
    }

    void Time::Update()
    {
        if (!s_Initialized)
        {
            Initialize();
        }

        // Reset per-frame bookkeeping.
        s_FixedStepsConsumedThisFrame = 0;

        const Clock::time_point now = Clock::now();
        const std::chrono::duration<double> unscaledDelta = now - s_LastFrameTime;
        s_LastFrameTime = now;

        double dt = unscaledDelta.count();

        if (!std::isfinite(dt) || dt < 0.0)
            dt = 0.0;

        // Clamp unscaled delta to avoid extremely large dt after stalls.
        const double maxDt = std::max(0.0, static_cast<double>(s_MaximumDeltaTimeSeconds));
        if (dt > maxDt)
            dt = maxDt;

        s_UnscaledDeltaTimeSecondsDouble = dt;
        s_UnscaledDeltaTimeSeconds = static_cast<float>(dt);

        const double scale = std::max(0.0, static_cast<double>(s_TimeScale));
        const double scaledDt = dt * scale;

        s_DeltaTimeSecondsDouble = scaledDt;
        s_DeltaTimeSeconds = static_cast<float>(scaledDt);

        // Accumulate startup times.
        s_UnscaledTimeSinceStartupSeconds += s_UnscaledDeltaTimeSeconds;
        s_TimeSinceStartupSeconds += s_DeltaTimeSeconds;

        // Fixed step accumulation uses *unscaled* time so fixed simulation runs while slow-mo / pause is applied.
        // If you want fixed step to also respect TimeScale, consume fixed steps using scaled delta instead.
        s_FixedTimeAccumulatorSeconds += s_UnscaledDeltaTimeSeconds;

        ++s_FrameCount;
    }

    void Time::SetTimeScale(float timeScale) noexcept
    {
        if (!std::isfinite(timeScale) || timeScale < 0.0f)
        {
            s_TimeScale = 0.0f;
            return;
        }
        s_TimeScale = timeScale;
    }

    void Time::SetMaximumDeltaTimeSeconds(float maxDeltaTimeSeconds) noexcept
    {
        if (!std::isfinite(maxDeltaTimeSeconds) || maxDeltaTimeSeconds < 0.0f)
        {
            s_MaximumDeltaTimeSeconds = 0.0f;
            return;
        }
        s_MaximumDeltaTimeSeconds = maxDeltaTimeSeconds;
    }

    void Time::SetFixedDeltaTimeSeconds(float fixedDeltaTimeSeconds) noexcept
    {
        if (!std::isfinite(fixedDeltaTimeSeconds) || fixedDeltaTimeSeconds <= 0.0f)
        {
            // Keep the existing value if invalid.
            return;
        }
        s_FixedDeltaTimeSeconds = fixedDeltaTimeSeconds;
    }

    bool Time::TryConsumeFixedStep() noexcept
    {
        // Guard against invalid configuration.
        if (s_FixedDeltaTimeSeconds <= 0.0f || !std::isfinite(s_FixedDeltaTimeSeconds))
            return false;

        // Prevent an unbounded number of fixed steps in one frame.
        // We track consumption implicitly by limiting how much accumulator we allow to drain per frame.
        // If you need exact counts, expose a "ConsumeFixedSteps(maxSteps)" API later.
        if (s_FixedStepsConsumedThisFrame >= s_MaximumFixedStepsPerFrame)
            return false;

        if (s_FixedTimeAccumulatorSeconds + 1e-6f < s_FixedDeltaTimeSeconds)
            return false;

        s_FixedTimeAccumulatorSeconds -= s_FixedDeltaTimeSeconds;
        ++s_FixedStepsConsumedThisFrame;
        return true;
    }
}

