#include "Scene/Scene.h"

#include "Core/ConfigManager.h"
#include "Core/Debug/Log.h"

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

        std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsMutex);
        DeferredStructuralMutation deferred{};
        deferred.Apply = std::move(mutation);
        if (debugName && debugName[0] != '\0')
            deferred.DebugName = debugName;
        else
            deferred.DebugName = "DeferredStructuralMutation";
        m_DeferredStructuralMutations.emplace_back(std::move(deferred));
        return true;
    }

    void Scene::FlushDeferredStructuralMutations()
    {
        std::deque<DeferredStructuralMutation> pending;
        {
            std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsMutex);
            if (m_DeferredStructuralMutations.empty())
                return;
            pending.swap(m_DeferredStructuralMutations);
        }

        const RuntimePhase previousPhase = m_RuntimePhase;
        m_RuntimePhase = RuntimePhase::Structural;
        m_IsApplyingDeferredStructuralMutations = true;
        bool appliedStructuralMutation = false;

        while (!pending.empty())
        {
            DeferredStructuralMutation deferred = std::move(pending.front());
            pending.pop_front();
            try
            {
                deferred.Apply(*this);
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

            if (pending.empty())
            {
                std::lock_guard<std::mutex> lock(m_DeferredStructuralMutationsMutex);
                if (!m_DeferredStructuralMutations.empty())
                    pending.swap(m_DeferredStructuralMutations);
            }
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
