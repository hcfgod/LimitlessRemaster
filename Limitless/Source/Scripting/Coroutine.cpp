#include "Scripting/Coroutine.h"

#include "Core/Debug/Log.h"
#include "Scripting/ScriptableEntity.h"

#include <algorithm>
#include <cmath>

namespace Limitless
{
    namespace
    {
        float SanitizeWaitDurationSeconds(float durationSeconds)
        {
            if (!std::isfinite(durationSeconds))
                return 0.0f;
            return std::max(0.0f, durationSeconds);
        }
    }

    CoroutineHandle Coroutine::Start(ScriptableEntity& owner, CoroutineRoutine routine)
    {
        if (!routine.IsValid())
            return {};

        ScriptableEntity::CoroutineState coroutineState{};
        coroutineState.Handle = CoroutineHandle{ owner.m_NextCoroutineIdentifier++ };
        coroutineState.Routine = std::move(routine);
        if (owner.m_NextCoroutineIdentifier == 0)
            owner.m_NextCoroutineIdentifier = 1;

        const bool isStillRunning = coroutineState.Routine.Resume();
        const auto* promise = coroutineState.Routine.GetPromise();
        if (!promise)
            return {};

        if (promise->UnhandledException)
        {
            try
            {
                std::rethrow_exception(promise->UnhandledException);
            }
            catch (const std::exception& exception)
            {
                LT_ERROR("Coroutine start failed: {}", exception.what());
            }
            catch (...)
            {
                LT_ERROR("Coroutine start failed with unknown exception.");
            }
            return {};
        }

        if (!isStillRunning)
            return {};

        coroutineState.WaitingForSeconds = promise->LastYieldKind == CoroutineRoutine::YieldKind::WaitForSeconds;
        coroutineState.WaitingForFrames = promise->LastYieldKind == CoroutineRoutine::YieldKind::WaitForFrames;
        coroutineState.RemainingWaitSeconds = coroutineState.WaitingForSeconds
            ? SanitizeWaitDurationSeconds(promise->WaitDurationSeconds)
            : 0.0f;
        coroutineState.RemainingWaitFrames = coroutineState.WaitingForFrames
            ? std::max(1u, promise->WaitFrameCount)
            : 0u;
        coroutineState.SkipWaitTickThisFrame = true;

        const CoroutineHandle newCoroutineHandle = coroutineState.Handle;
        if (owner.m_IsAdvancingCoroutines)
            owner.m_PendingCoroutineStarts.emplace_back(std::move(coroutineState));
        else
            owner.m_ActiveCoroutines.emplace_back(std::move(coroutineState));
        return newCoroutineHandle;
    }

    bool Coroutine::Stop(ScriptableEntity& owner, CoroutineHandle coroutineHandle)
    {
        if (!coroutineHandle.IsValid())
            return false;

        const auto matchesHandle = [coroutineHandle](const ScriptableEntity::CoroutineState& coroutineState) {
            return coroutineState.Handle == coroutineHandle;
        };

        const bool existsInActive = std::any_of(owner.m_ActiveCoroutines.begin(), owner.m_ActiveCoroutines.end(), matchesHandle);
        const bool existsInPendingStarts = std::any_of(owner.m_PendingCoroutineStarts.begin(), owner.m_PendingCoroutineStarts.end(), matchesHandle);
        if (!existsInActive && !existsInPendingStarts)
            return false;

        if (owner.m_IsAdvancingCoroutines)
        {
            const bool alreadyQueued = std::any_of(owner.m_PendingCoroutineStops.begin(),
                                                   owner.m_PendingCoroutineStops.end(),
                                                   [coroutineHandle](const CoroutineHandle& queuedHandle) {
                                                       return queuedHandle == coroutineHandle;
                                                   });
            if (!alreadyQueued)
                owner.m_PendingCoroutineStops.push_back(coroutineHandle);
            return true;
        }

        owner.m_ActiveCoroutines.erase(
            std::remove_if(owner.m_ActiveCoroutines.begin(), owner.m_ActiveCoroutines.end(), matchesHandle),
            owner.m_ActiveCoroutines.end());

        owner.m_PendingCoroutineStarts.erase(
            std::remove_if(owner.m_PendingCoroutineStarts.begin(), owner.m_PendingCoroutineStarts.end(), matchesHandle),
            owner.m_PendingCoroutineStarts.end());
        return true;
    }

    void Coroutine::StopAll(ScriptableEntity& owner)
    {
        if (owner.m_IsAdvancingCoroutines)
        {
            owner.m_PendingCoroutineStarts.clear();
            for (const auto& coroutineState : owner.m_ActiveCoroutines)
            {
                const bool alreadyQueued = std::any_of(owner.m_PendingCoroutineStops.begin(),
                                                       owner.m_PendingCoroutineStops.end(),
                                                       [&coroutineState](const CoroutineHandle& queuedHandle) {
                                                           return queuedHandle == coroutineState.Handle;
                                                       });
                if (!alreadyQueued)
                    owner.m_PendingCoroutineStops.push_back(coroutineState.Handle);
            }
            return;
        }

        owner.m_ActiveCoroutines.clear();
        owner.m_PendingCoroutineStarts.clear();
        owner.m_PendingCoroutineStops.clear();
    }

    bool Coroutine::IsRunning(const ScriptableEntity& owner, CoroutineHandle coroutineHandle)
    {
        if (!coroutineHandle.IsValid())
            return false;

        const auto matchesHandle = [coroutineHandle](const ScriptableEntity::CoroutineState& coroutineState) {
            return coroutineState.Handle == coroutineHandle;
        };

        const bool active = std::any_of(owner.m_ActiveCoroutines.begin(), owner.m_ActiveCoroutines.end(), matchesHandle);
        if (!active)
            return std::any_of(owner.m_PendingCoroutineStarts.begin(), owner.m_PendingCoroutineStarts.end(), matchesHandle);

        const bool queuedForStop = std::any_of(owner.m_PendingCoroutineStops.begin(),
                                               owner.m_PendingCoroutineStops.end(),
                                               [coroutineHandle](const CoroutineHandle& pendingStop) {
                                                   return pendingStop == coroutineHandle;
                                               });
        return !queuedForStop;
    }

    void Coroutine::TickOwner(ScriptableEntity& owner, float deltaTimeSeconds)
    {
        const float safeDeltaTime = SanitizeWaitDurationSeconds(deltaTimeSeconds);
        owner.m_IsAdvancingCoroutines = true;

        for (size_t coroutineIndex = 0; coroutineIndex < owner.m_ActiveCoroutines.size();)
        {
            auto& coroutineState = owner.m_ActiveCoroutines[coroutineIndex];
            const bool queuedForStop = std::any_of(owner.m_PendingCoroutineStops.begin(),
                                                   owner.m_PendingCoroutineStops.end(),
                                                   [&coroutineState](const CoroutineHandle& pendingStop) {
                                                       return pendingStop == coroutineState.Handle;
                                                   });
            if (queuedForStop)
            {
                owner.m_ActiveCoroutines.erase(owner.m_ActiveCoroutines.begin() + static_cast<std::ptrdiff_t>(coroutineIndex));
                continue;
            }

            if (coroutineState.SkipWaitTickThisFrame)
            {
                coroutineState.SkipWaitTickThisFrame = false;
                ++coroutineIndex;
                continue;
            }

            bool stillWaiting = false;
            if (coroutineState.WaitingForFrames)
            {
                if (coroutineState.RemainingWaitFrames > 0)
                    --coroutineState.RemainingWaitFrames;
                stillWaiting = coroutineState.RemainingWaitFrames > 0;
            }
            else if (coroutineState.WaitingForSeconds)
            {
                coroutineState.RemainingWaitSeconds = std::max(0.0f, coroutineState.RemainingWaitSeconds - safeDeltaTime);
                stillWaiting = coroutineState.RemainingWaitSeconds > 0.0f;
            }

            if (stillWaiting)
            {
                ++coroutineIndex;
                continue;
            }

            coroutineState.WaitingForSeconds = false;
            coroutineState.WaitingForFrames = false;
            coroutineState.RemainingWaitSeconds = 0.0f;
            coroutineState.RemainingWaitFrames = 0;

            const bool isStillRunning = coroutineState.Routine.Resume();
            const auto* promise = coroutineState.Routine.GetPromise();
            if (promise && promise->UnhandledException)
            {
                try
                {
                    std::rethrow_exception(promise->UnhandledException);
                }
                catch (const std::exception& exception)
                {
                    LT_ERROR("Coroutine failed: {}", exception.what());
                }
                catch (...)
                {
                    LT_ERROR("Coroutine failed with unknown exception.");
                }

                owner.m_ActiveCoroutines.erase(owner.m_ActiveCoroutines.begin() + static_cast<std::ptrdiff_t>(coroutineIndex));
                continue;
            }

            if (!isStillRunning || !promise)
            {
                owner.m_ActiveCoroutines.erase(owner.m_ActiveCoroutines.begin() + static_cast<std::ptrdiff_t>(coroutineIndex));
                continue;
            }

            coroutineState.WaitingForSeconds = promise->LastYieldKind == CoroutineRoutine::YieldKind::WaitForSeconds;
            coroutineState.WaitingForFrames = promise->LastYieldKind == CoroutineRoutine::YieldKind::WaitForFrames;
            coroutineState.RemainingWaitSeconds = coroutineState.WaitingForSeconds
                ? SanitizeWaitDurationSeconds(promise->WaitDurationSeconds)
                : 0.0f;
            coroutineState.RemainingWaitFrames = coroutineState.WaitingForFrames
                ? std::max(1u, promise->WaitFrameCount)
                : 0u;
            ++coroutineIndex;
        }

        owner.m_IsAdvancingCoroutines = false;

        if (!owner.m_PendingCoroutineStops.empty())
        {
            owner.m_ActiveCoroutines.erase(
                std::remove_if(owner.m_ActiveCoroutines.begin(),
                               owner.m_ActiveCoroutines.end(),
                               [&owner](const ScriptableEntity::CoroutineState& coroutineState) {
                                   return std::any_of(owner.m_PendingCoroutineStops.begin(),
                                                      owner.m_PendingCoroutineStops.end(),
                                                      [&coroutineState](const CoroutineHandle& pendingStop) {
                                                          return pendingStop == coroutineState.Handle;
                                                      });
                               }),
                owner.m_ActiveCoroutines.end());

            owner.m_PendingCoroutineStarts.erase(
                std::remove_if(owner.m_PendingCoroutineStarts.begin(),
                               owner.m_PendingCoroutineStarts.end(),
                               [&owner](const ScriptableEntity::CoroutineState& coroutineState) {
                                   return std::any_of(owner.m_PendingCoroutineStops.begin(),
                                                      owner.m_PendingCoroutineStops.end(),
                                                      [&coroutineState](const CoroutineHandle& pendingStop) {
                                                          return pendingStop == coroutineState.Handle;
                                                      });
                               }),
                owner.m_PendingCoroutineStarts.end());

            owner.m_PendingCoroutineStops.clear();
        }

        if (!owner.m_PendingCoroutineStarts.empty())
        {
            for (auto& coroutineState : owner.m_PendingCoroutineStarts)
                owner.m_ActiveCoroutines.emplace_back(std::move(coroutineState));
            owner.m_PendingCoroutineStarts.clear();
        }
    }
}
