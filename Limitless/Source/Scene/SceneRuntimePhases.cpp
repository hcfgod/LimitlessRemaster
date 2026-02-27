#include "Scene/Scene.h"

#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

#include <algorithm>
#include <exception>

namespace Limitless
{
    namespace
    {
        thread_local bool s_IsParallelScriptExecutionThread = false;
    }

    bool Scene::IsCurrentThreadParallelScriptExecution()
    {
        return s_IsParallelScriptExecutionThread;
    }

    void Scene::SetCurrentThreadParallelScriptExecution(bool enabled)
    {
        s_IsParallelScriptExecutionThread = enabled;
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

        if (m_DeferredStructuralMutationQueue && m_DeferredStructuralMutationQueue->TryPush(std::move(deferred)))
            return true;

        {
            std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsOverflowMutex);
            m_DeferredStructuralMutationsOverflow.emplace_back(std::move(deferred));
        }

        bool expectedWarnState = false;
        if (m_WarnedDeferredStructuralMutationQueueOverflow.compare_exchange_strong(expectedWarnState, true, std::memory_order_relaxed))
        {
            LT_WARN("Scene deferred structural mutation queue overflowed; falling back to overflow buffer. Consider increasing queue size.");
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

        std::vector<DeferredStructuralMutation> pending;
        drainPendingMutations(pending);
        if (pending.empty())
            return;

        const RuntimePhase previousPhase = m_RuntimePhase;
        m_RuntimePhase = RuntimePhase::Structural;
        m_IsApplyingDeferredStructuralMutations = true;
        bool appliedStructuralMutation = false;

        while (true)
        {
            for (DeferredStructuralMutation& deferred : pending)
            {
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

            drainPendingMutations(pending);
            if (pending.empty())
                break;
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
