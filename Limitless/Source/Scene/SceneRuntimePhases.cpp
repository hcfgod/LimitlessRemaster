#include "Scene/Scene.h"

#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <stdexcept>

namespace Limitless
{
    namespace
    {
        thread_local bool s_IsParallelScriptExecutionThread = false;

        bool IsMutableRegistryAccessValidationEnabled()
        {
            return ConfigManager::GetInstance().GetValue<bool>("ecs.mt.validate_mutable_registry_access", true);
        }
    }

    bool Scene::IsCurrentThreadParallelScriptExecution()
    {
        return s_IsParallelScriptExecutionThread;
    }

    void Scene::SetCurrentThreadParallelScriptExecution(bool enabled)
    {
        s_IsParallelScriptExecutionThread = enabled;
    }

    entt::registry& Scene::GetRegistry()
    {
        if (IsMutableRegistryAccessValidationEnabled() && IsCurrentThreadParallelScriptExecution())
        {
            const RuntimePhase phase = m_RuntimePhase;
            if (phase != RuntimePhase::Idle && phase != RuntimePhase::Structural && !m_IsApplyingDeferredStructuralMutations)
            {
                const uint32_t phaseIndex = static_cast<uint32_t>(phase);
                if (phaseIndex < 32)
                {
                    const uint32_t phaseBit = (1u << phaseIndex);
                    const uint32_t warnedPhases = m_WarnedUnsafeMutableRegistryAccessPhases.fetch_or(phaseBit, std::memory_order_relaxed);
                    if ((warnedPhases & phaseBit) == 0)
                    {
                        LT_WARN("Scene::GetRegistry mutable access during runtime phase {} on a parallel script thread can bypass deferred structural safeguards. Prefer Scene/Entity structural APIs.",
                                phaseIndex);
                    }
                }

                throw std::runtime_error("Scene::GetRegistry mutable access is not supported during parallel script execution");
            }
        }

        return m_Registry;
    }

    bool Scene::ShouldDeferStructuralMutations() const
    {
        const bool enableDeferredStructuralMutations = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.defer_structural_mutations", true);
        if (!enableDeferredStructuralMutations)
            return false;
        if (m_IsApplyingDeferredStructuralMutations)
            return false;
        if (m_RuntimePhase == RuntimePhase::Structural)
            return false;
        return IsCurrentThreadParallelScriptExecution();
    }

    bool Scene::EnqueueDeferredStructuralMutation(std::function<void(Scene&)> mutation, const char* debugName)
    {
        if (!mutation)
            return false;

        DeferredStructuralMutation deferred{};
        deferred.Sequence = m_NextDeferredStructuralMutationSequence.fetch_add(1, std::memory_order_relaxed);
        deferred.Apply = [this, mutation = std::move(mutation)]() mutable {
            mutation(*this);
        };
        if (debugName && debugName[0] != '\0')
            deferred.DebugName = debugName;
        else
            deferred.DebugName = "DeferredStructuralMutation";

        if (m_DeferredStructuralMutationQueue)
        {
            DeferredStructuralMutation queuedMutation = deferred;
            if (m_DeferredStructuralMutationQueue->TryPush(std::move(queuedMutation)))
                return true;
        }

        if (m_DeferredStructuralMutationOverflowQueue)
        {
            DeferredStructuralMutation queuedMutation = deferred;
            if (m_DeferredStructuralMutationOverflowQueue->TryPush(std::move(queuedMutation)))
                return true;
        }

        {
            std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsOverflowMutex);
            m_DeferredStructuralMutationsOverflow.emplace_back(std::move(deferred));
        }

        bool expectedWarnState = false;
        if (m_WarnedDeferredStructuralMutationQueueOverflow.compare_exchange_strong(expectedWarnState, true, std::memory_order_relaxed))
        {
            LT_WARN("Scene deferred structural mutation queue overflowed; falling back to tertiary overflow buffer guarded by a mutex. Consider increasing queue size.");
        }
        return true;
    }

    void Scene::FlushDeferredStructuralMutations()
    {
        auto drainPendingMutations = [this](std::vector<DeferredStructuralMutation>& pending) {
            pending.clear();

            if (m_DeferredStructuralMutationQueue)
            {
                while (auto queued = m_DeferredStructuralMutationQueue->TryPop())
                    pending.emplace_back(std::move(*queued));
            }

            if (m_DeferredStructuralMutationOverflowQueue)
            {
                while (auto overflowQueued = m_DeferredStructuralMutationOverflowQueue->TryPop())
                    pending.emplace_back(std::move(*overflowQueued));
            }

            {
                std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsOverflowMutex);
                if (!m_DeferredStructuralMutationsOverflow.empty())
                {
                    pending.reserve(pending.size() + m_DeferredStructuralMutationsOverflow.size());
                    while (!m_DeferredStructuralMutationsOverflow.empty())
                    {
                        pending.emplace_back(std::move(m_DeferredStructuralMutationsOverflow.front()));
                        m_DeferredStructuralMutationsOverflow.pop_front();
                    }
                }
            }

            if (!pending.empty())
            {
                std::sort(pending.begin(), pending.end(), [](const DeferredStructuralMutation& left, const DeferredStructuralMutation& right) {
                    return left.Sequence < right.Sequence;
                });
            }
        };

        auto pushBackPendingMutations = [this](std::vector<DeferredStructuralMutation>& pending) {
            if (pending.empty())
                return;

            std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsOverflowMutex);
            for (DeferredStructuralMutation& deferred : pending)
                m_DeferredStructuralMutationsOverflow.emplace_back(std::move(deferred));
            pending.clear();
        };

        auto mergePendingMutations = [](std::vector<DeferredStructuralMutation>& pending,
                                        std::vector<DeferredStructuralMutation>& newlyDrained) {
            if (newlyDrained.empty())
                return;
            if (pending.empty())
            {
                pending = std::move(newlyDrained);
                return;
            }

            std::vector<DeferredStructuralMutation> merged;
            merged.reserve(pending.size() + newlyDrained.size());

            size_t pendingIndex = 0;
            size_t drainedIndex = 0;
            while (pendingIndex < pending.size() && drainedIndex < newlyDrained.size())
            {
                if (pending[pendingIndex].Sequence <= newlyDrained[drainedIndex].Sequence)
                    merged.emplace_back(std::move(pending[pendingIndex++]));
                else
                    merged.emplace_back(std::move(newlyDrained[drainedIndex++]));
            }

            while (pendingIndex < pending.size())
                merged.emplace_back(std::move(pending[pendingIndex++]));
            while (drainedIndex < newlyDrained.size())
                merged.emplace_back(std::move(newlyDrained[drainedIndex++]));

            pending = std::move(merged);
            newlyDrained.clear();
        };

        std::vector<DeferredStructuralMutation> pending;
        drainPendingMutations(pending);
        if (pending.empty())
            return;

        const uint32_t configuredBudget = ConfigManager::GetInstance().GetValue<uint32_t>(
            "ecs.mt.deferred_structural_mutation_flush_budget",
            kDefaultDeferredStructuralMutationFlushBudget);
        const size_t remainingBudgetInitial = std::max<size_t>(1, static_cast<size_t>(configuredBudget));

        const RuntimePhase previousPhase = m_RuntimePhase;
        m_RuntimePhase = RuntimePhase::Structural;
        m_IsApplyingDeferredStructuralMutations = true;
        bool appliedStructuralMutation = false;
        size_t remainingBudget = remainingBudgetInitial;
        size_t processedMutations = 0;

        while (!pending.empty() && remainingBudget > 0)
        {
            const size_t mutationsToProcess = std::min(remainingBudget, pending.size());
            for (size_t mutationIndex = 0; mutationIndex < mutationsToProcess; ++mutationIndex)
            {
                DeferredStructuralMutation& deferred = pending[mutationIndex];
                try
                {
                    deferred.Apply();
                    appliedStructuralMutation = true;
                }
                catch (const std::exception& exception)
                {
                    LT_ERROR("Scene deferred structural mutation '{}' failed: {}", deferred.DebugName, exception.what());
                }
                catch (...)
                {
                    LT_ERROR("Scene deferred structural mutation '{}' failed with unknown exception", deferred.DebugName);
                }
            }

            processedMutations += mutationsToProcess;
            remainingBudget -= mutationsToProcess;

            if (mutationsToProcess < pending.size())
                pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(mutationsToProcess));
            else
                pending.clear();

            std::vector<DeferredStructuralMutation> newlyDrained;
            drainPendingMutations(newlyDrained);
            mergePendingMutations(pending, newlyDrained);
        }

        const size_t deferredRemainder = pending.size();
        if (deferredRemainder > 0)
        {
            pushBackPendingMutations(pending);
            bool expectedWarnState = false;
            if (m_WarnedDeferredStructuralMutationFlushBudgetExceeded.compare_exchange_strong(expectedWarnState, true, std::memory_order_relaxed))
            {
                LT_WARN("Scene deferred structural mutation flush budget exceeded (budget={}, processed={}, deferred_remainder={}). "
                        "Remaining structural mutations will be carried into a future flush.",
                        remainingBudgetInitial,
                        processedMutations,
                        deferredRemainder);
            }
        }
        else
        {
            m_WarnedDeferredStructuralMutationFlushBudgetExceeded.store(false, std::memory_order_relaxed);
        }

        m_IsApplyingDeferredStructuralMutations = false;
        m_RuntimePhase = previousPhase;

        if (appliedStructuralMutation)
        {
            m_TransformsDirty = true;
            m_HierarchyDepthDirty = true;
        }
    }
}
