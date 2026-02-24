#include "Scripting/ScriptableEntity.h"

#include "Scene/Components.h"
#include "Scene/ParticleEmitterSystem.h"

#ifndef SCRIPTCORE_EXPORTS
    #include "Physics/Physics2DQueries.h"
    #include "Scene/Scene.h"
#endif

#include <algorithm>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        ScriptCreateEntityBridgeCallback s_CreateEntityBridgeCallback = nullptr;
        ScriptDestroyEntityBridgeCallback s_DestroyEntityBridgeCallback = nullptr;
        ScriptInstantiatePrefabBridgeCallback s_InstantiatePrefabBridgeCallback = nullptr;

        std::string GetUnqualifiedClassName(const std::string& className)
        {
            const size_t separator = className.rfind("::");
            if (separator == std::string::npos)
                return className;
            return className.substr(separator + 2);
        }

        bool ScriptClassNamesMatch(const std::string& left, const std::string& right)
        {
            if (left == right)
                return true;
            return GetUnqualifiedClassName(left) == GetUnqualifiedClassName(right);
        }
    }

    void ScriptableEntity::SetCreateEntityBridgeCallback(ScriptCreateEntityBridgeCallback callback)
    {
        s_CreateEntityBridgeCallback = callback;
    }

    void ScriptableEntity::SetDestroyEntityBridgeCallback(ScriptDestroyEntityBridgeCallback callback)
    {
        s_DestroyEntityBridgeCallback = callback;
    }

    void ScriptableEntity::SetInstantiatePrefabBridgeCallback(ScriptInstantiatePrefabBridgeCallback callback)
    {
        s_InstantiatePrefabBridgeCallback = callback;
    }

    Entity ScriptableEntity::CreateEntity(const std::string& name)
    {
        return Entity(m_Registry, CreateEntityHandle(name));
    }

    entt::entity ScriptableEntity::CreateEntityHandle(const std::string& name)
    {
        const char* requestedName = name.empty() ? "Entity" : name.c_str();
        if (s_CreateEntityBridgeCallback)
            return s_CreateEntityBridgeCallback(requestedName);

#ifndef SCRIPTCORE_EXPORTS
        if (m_Scene)
            return m_Scene->CreateEntity(requestedName);
#endif
        return entt::null;
    }

    Entity ScriptableEntity::Instantiate(const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (prefabAssetKey.empty())
            return Entity{};

        if (s_InstantiatePrefabBridgeCallback)
            return Entity(m_Registry, s_InstantiatePrefabBridgeCallback(prefabAssetKey.c_str(), parentEntity));

#ifndef SCRIPTCORE_EXPORTS
        if (m_Scene)
            return Entity(m_Registry, m_Scene->InstantiatePrefab(prefabAssetKey, parentEntity));
#endif
        return Entity{};
    }

    Entity ScriptableEntity::Instantiate(const std::string& prefabAssetKey, const Entity& parentEntity)
    {
        const entt::entity parentHandle = (parentEntity && m_Registry && m_Registry->valid(parentEntity.GetHandle()))
            ? parentEntity.GetHandle()
            : entt::null;
        return Instantiate(prefabAssetKey, parentHandle);
    }

    Entity ScriptableEntity::Instantiate(const Prefab& prefabReference, entt::entity parentEntity)
    {
        return Instantiate(prefabReference.AssetKey, parentEntity);
    }

    Entity ScriptableEntity::Instantiate(const Prefab& prefabReference, const Entity& parentEntity)
    {
        return Instantiate(prefabReference.AssetKey, parentEntity);
    }

    Entity ScriptableEntity::Instantiate(const Entity& prefabReference, entt::entity parentEntity)
    {
        if (prefabReference.IsPrefabReference())
            return Instantiate(prefabReference.GetPrefabAssetKey(), parentEntity);

        if (!prefabReference || !m_Registry || !m_Registry->valid(prefabReference.GetHandle()))
            return Entity{};

        if (const auto* prefabInstance = m_Registry->try_get<PrefabInstanceComponent>(prefabReference.GetHandle()))
            return Instantiate(prefabInstance->PrefabAssetKey, parentEntity);

        return Entity{};
    }

    Entity ScriptableEntity::Instantiate(const Entity& prefabReference, const Entity& parentEntity)
    {
        const entt::entity parentHandle = (parentEntity && m_Registry && m_Registry->valid(parentEntity.GetHandle()))
            ? parentEntity.GetHandle()
            : entt::null;
        return Instantiate(prefabReference, parentHandle);
    }

    void ScriptableEntity::DestroyEntity(Entity entity)
    {
        DestroyEntity(entity.GetHandle());
    }

    void ScriptableEntity::DestroyEntity(entt::entity entity)
    {
        if (entity == entt::null)
            return;

        if (s_DestroyEntityBridgeCallback)
        {
            s_DestroyEntityBridgeCallback(entity);
            return;
        }

#ifndef SCRIPTCORE_EXPORTS
        if (m_Scene)
            m_Scene->DestroyEntity(entity);
#endif
    }

    bool ScriptableEntity::IsEntityValid(entt::entity entity) const
    {
        if (entity == entt::null)
            return false;
        if (m_Registry == nullptr)
            return false;
        return m_Registry->valid(entity);
    }

    Entity ScriptableEntity::GetEntity(entt::entity entity) const
    {
        if (!IsEntityValid(entity))
            return Entity{};
        return Entity(m_Registry, entity);
    }

    Entity ScriptableEntity::FindEntityByTag(const std::string& tag) const
    {
        if (tag.empty() || m_Registry == nullptr)
            return Entity{};

        auto view = m_Registry->view<TagComponent>();
        for (entt::entity entity : view)
        {
            const auto& tagComponent = view.get<TagComponent>(entity);
            if (tagComponent.Tag == tag)
                return Entity(m_Registry, entity);
        }

        return Entity{};
    }

    Entity ScriptableEntity::GetParent(const Entity& entity) const
    {
        return GetParent(entity.GetHandle());
    }

    Entity ScriptableEntity::GetParent(entt::entity entity) const
    {
        if (!IsEntityValid(entity))
            return Entity{};

        const auto* hierarchy = m_Registry->try_get<HierarchyComponent>(entity);
        if (!hierarchy || hierarchy->Parent == entt::null || !m_Registry->valid(hierarchy->Parent))
            return Entity{};

        return Entity(m_Registry, hierarchy->Parent);
    }

    std::vector<Entity> ScriptableEntity::GetChildren(const Entity& parent) const
    {
        return GetChildren(parent.GetHandle());
    }

    std::vector<Entity> ScriptableEntity::GetChildren(entt::entity parent) const
    {
        std::vector<std::pair<int32_t, entt::entity>> orderedChildren;
        if (m_Registry == nullptr)
            return {};

        auto hierarchyView = m_Registry->view<HierarchyComponent>();
        for (entt::entity candidate : hierarchyView)
        {
            if (!m_Registry->valid(candidate))
                continue;
            const auto& hierarchy = hierarchyView.get<HierarchyComponent>(candidate);
            if (hierarchy.Parent == parent)
                orderedChildren.emplace_back(hierarchy.SiblingOrder, candidate);
        }

        std::sort(orderedChildren.begin(), orderedChildren.end(), [](const auto& left, const auto& right) {
            if (left.first != right.first)
                return left.first < right.first;
            return static_cast<uint32_t>(left.second) < static_cast<uint32_t>(right.second);
        });

        std::vector<Entity> children;
        children.reserve(orderedChildren.size());
        for (const auto& [siblingOrder, child] : orderedChildren)
        {
            (void)siblingOrder;
            children.emplace_back(m_Registry, child);
        }
        return children;
    }

    std::vector<Entity> ScriptableEntity::GetHierarchy(const Entity& root, bool includeRoot) const
    {
        return GetHierarchy(root.GetHandle(), includeRoot);
    }

    std::vector<Entity> ScriptableEntity::GetHierarchy(entt::entity root, bool includeRoot) const
    {
        if (!IsEntityValid(root))
            return {};

        std::unordered_multimap<entt::entity, std::pair<int32_t, entt::entity>> childrenByParent;
        auto hierarchyView = m_Registry->view<HierarchyComponent>();
        for (entt::entity candidate : hierarchyView)
        {
            if (!m_Registry->valid(candidate))
                continue;
            const auto& hierarchy = hierarchyView.get<HierarchyComponent>(candidate);
            childrenByParent.emplace(hierarchy.Parent, std::make_pair(hierarchy.SiblingOrder, candidate));
        }

        std::vector<entt::entity> queue;
        queue.push_back(root);

        std::vector<Entity> result;
        if (includeRoot)
            result.emplace_back(m_Registry, root);

        for (size_t index = 0; index < queue.size(); ++index)
        {
            const entt::entity parent = queue[index];
            std::vector<std::pair<int32_t, entt::entity>> orderedChildren;
            const auto [childBegin, childEnd] = childrenByParent.equal_range(parent);
            for (auto iterator = childBegin; iterator != childEnd; ++iterator)
            {
                const auto& [siblingOrder, child] = iterator->second;
                if (m_Registry->valid(child))
                    orderedChildren.emplace_back(siblingOrder, child);
            }

            std::sort(orderedChildren.begin(), orderedChildren.end(), [](const auto& left, const auto& right) {
                if (left.first != right.first)
                    return left.first < right.first;
                return static_cast<uint32_t>(left.second) < static_cast<uint32_t>(right.second);
            });

            for (const auto& [siblingOrder, child] : orderedChildren)
            {
                (void)siblingOrder;
                queue.push_back(child);
                result.emplace_back(m_Registry, child);
            }
        }

        return result;
    }

    std::vector<ScriptableEntity*> ScriptableEntity::GetRuntimeScripts(entt::entity entity) const
    {
        if (m_Registry == nullptr || entity == entt::null || !m_Registry->valid(entity))
            return {};

        const auto* nativeScriptComponent = m_Registry->try_get<NativeScriptComponent>(entity);
        if (!nativeScriptComponent)
            return {};

        std::vector<ScriptableEntity*> scripts;
        scripts.reserve(nativeScriptComponent->Scripts.size());
        for (const auto& scriptEntry : nativeScriptComponent->Scripts)
        {
            if (!scriptEntry.Enabled || scriptEntry.ScriptClassName.empty() || !scriptEntry.RuntimeInstance)
                continue;
            scripts.push_back(scriptEntry.RuntimeInstance.get());
        }
        return scripts;
    }

    ScriptableEntity* ScriptableEntity::GetScript(const std::string& className)
    {
        return GetScript(m_EntityHandle, className);
    }

    ScriptableEntity* ScriptableEntity::GetScript(const Entity& entity, const std::string& className)
    {
        return GetScript(entity.GetHandle(), className);
    }

    ScriptableEntity* ScriptableEntity::GetScript(entt::entity entity, const std::string& className)
    {
        if (className.empty() || m_Registry == nullptr || entity == entt::null || !m_Registry->valid(entity))
            return nullptr;

        const auto* nativeScriptComponent = m_Registry->try_get<NativeScriptComponent>(entity);
        if (!nativeScriptComponent)
            return nullptr;

        for (const auto& scriptEntry : nativeScriptComponent->Scripts)
        {
            if (!scriptEntry.Enabled || scriptEntry.ScriptClassName.empty() || !scriptEntry.RuntimeInstance)
                continue;
            if (ScriptClassNamesMatch(scriptEntry.ScriptClassName, className))
                return scriptEntry.RuntimeInstance.get();
        }
        return nullptr;
    }

    std::vector<ScriptableEntity*> ScriptableEntity::GetScripts()
    {
        return GetRuntimeScripts(m_EntityHandle);
    }

    std::vector<ScriptableEntity*> ScriptableEntity::GetScripts(const Entity& entity)
    {
        return GetRuntimeScripts(entity.GetHandle());
    }

    std::vector<ScriptableEntity*> ScriptableEntity::GetScripts(entt::entity entity)
    {
        return GetRuntimeScripts(entity);
    }

    float ScriptableEntity::GetExposedFloat(const std::string& name, float fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<float>(&found->second))
            return *value;
        return fallbackValue;
    }

    int32_t ScriptableEntity::GetExposedInteger(const std::string& name, int32_t fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<int32_t>(&found->second))
            return *value;
        return fallbackValue;
    }

    bool ScriptableEntity::GetExposedBoolean(const std::string& name, bool fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<bool>(&found->second))
            return *value;
        return fallbackValue;
    }

    glm::vec3 ScriptableEntity::GetExposedVector3(const std::string& name, const glm::vec3& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<glm::vec3>(&found->second))
            return *value;
        return fallbackValue;
    }

    std::string ScriptableEntity::GetExposedString(const std::string& name, const std::string& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<std::string>(&found->second))
            return *value;
        return fallbackValue;
    }

    Entity ScriptableEntity::GetExposedEntity(const std::string& name, const Entity& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<ScriptEntityReference>(&found->second))
        {
            if (!value->PrefabAssetKey.empty())
                return Entity::FromPrefabAssetKey(value->PrefabAssetKey);
            if (!value->Tag.empty())
            {
                const Entity resolved = FindEntityByTag(value->Tag);
                if (resolved)
                    return resolved;
            }
            return Entity{};
        }
        return fallbackValue;
    }

    Prefab ScriptableEntity::GetExposedPrefab(const std::string& name, const Prefab& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<Prefab>(&found->second))
            return *value;
        if (const auto* entityValue = std::get_if<ScriptEntityReference>(&found->second))
        {
            if (!entityValue->PrefabAssetKey.empty())
                return Prefab{ entityValue->PrefabAssetKey };
        }
        return fallbackValue;
    }

    void ScriptableEntity::SetExposedFloat(const std::string& name, float value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedInteger(const std::string& name, int32_t value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedBoolean(const std::string& name, bool value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedVector3(const std::string& name, const glm::vec3& value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedString(const std::string& name, const std::string& value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedEntity(const std::string& name, const Entity& value)
    {
        if (!m_ExposedProperties)
            return;

        ScriptEntityReference entityReference{};
        if (value.IsPrefabReference())
        {
            entityReference.PrefabAssetKey = value.GetPrefabAssetKey();
        }
        else if (value && m_Registry && m_Registry->valid(value.GetHandle()))
        {
            if (const auto* tagComponent = m_Registry->try_get<TagComponent>(value.GetHandle()))
                entityReference.Tag = tagComponent->Tag;
        }
        (*m_ExposedProperties)[name] = std::move(entityReference);
    }

    void ScriptableEntity::SetExposedPrefab(const std::string& name, const Prefab& value)
    {
        if (!m_ExposedProperties)
            return;
        (*m_ExposedProperties)[name] = value;
    }

    bool ScriptableEntity::Raycast2D(const glm::vec2& origin,
                                     const glm::vec2& direction,
                                     float maxDistance,
                                     entt::entity& outEntity,
                                     glm::vec2& outPoint,
                                     glm::vec2& outNormal,
                                     float& outFraction,
                                     uint64_t collisionMask) const
    {
#ifdef SCRIPTCORE_EXPORTS
        (void)origin;
        (void)direction;
        (void)maxDistance;
        (void)outEntity;
        (void)outPoint;
        (void)outNormal;
        (void)outFraction;
        (void)collisionMask;
        return false;
#else
        if (!m_Scene)
            return false;

        const Physics2DRaycastHit hit = Physics2DQueries::RaycastClosest(m_Scene, origin, direction, maxDistance, collisionMask);
        if (!hit.HasHit)
            return false;

        outEntity = hit.Entity;
        outPoint = hit.Point;
        outNormal = hit.Normal;
        outFraction = hit.Fraction;
        return true;
#endif
    }

    bool ScriptableEntity::HasContactWith(entt::entity otherEntity, bool includeSensorContacts) const
    {
#ifdef SCRIPTCORE_EXPORTS
        (void)otherEntity;
        (void)includeSensorContacts;
        return false;
#else
        if (!m_Scene)
            return false;
        const Physics2DContactListener* contacts = m_Scene->GetPhysics2DContactEventsForEntity(m_EntityHandle);
        if (!contacts)
            return false;

        const auto& events = contacts->GetEvents();
        for (const auto& eventData : events)
        {
            if (!includeSensorContacts && eventData.IsSensor)
                continue;
            const bool matchesPair = (eventData.EntityA == m_EntityHandle && eventData.EntityB == otherEntity) ||
                                     (eventData.EntityB == m_EntityHandle && eventData.EntityA == otherEntity);
            if (matchesPair)
                return true;
        }

        return false;
#endif
    }

    int ScriptableEntity::GetContactCount(bool includeSensorContacts) const
    {
#ifdef SCRIPTCORE_EXPORTS
        (void)includeSensorContacts;
        return 0;
#else
        if (!m_Scene)
            return 0;
        const Physics2DContactListener* contacts = m_Scene->GetPhysics2DContactEventsForEntity(m_EntityHandle);
        if (!contacts)
            return 0;

        int contactCount = 0;
        const auto& events = contacts->GetEvents();
        for (const auto& eventData : events)
        {
            if (!includeSensorContacts && eventData.IsSensor)
                continue;
            if (eventData.EntityA == m_EntityHandle || eventData.EntityB == m_EntityHandle)
                ++contactCount;
        }
        return contactCount;
#endif
    }

    bool ScriptableEntity::HasAnimator() const
    {
        return m_Registry != nullptr && m_Registry->valid(m_EntityHandle) && m_Registry->all_of<AnimatorComponent>(m_EntityHandle);
    }

    bool ScriptableEntity::PlayAnimatorState(const std::string& stateName, bool restartIfSameState)
    {
        if (!HasAnimator() || stateName.empty())
            return false;

        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        if (!restartIfSameState && animator.RuntimeCurrentStateName == stateName)
            return true;

        animator.RuntimeCurrentStateName = stateName;
        animator.RuntimeCurrentClipKey.clear();
        animator.RuntimeStateTimeSeconds = 0.0f;
        animator.RuntimePreviousStateTimeSeconds = 0.0f;
        animator.RuntimeCurrentStateDurationSeconds = 1.0f;
        animator.RuntimeInitialized = true;
        return true;
    }

    bool ScriptableEntity::PlayAnimatorClip(const std::string& clipKey, bool restartIfSameClip)
    {
        if (!HasAnimator() || clipKey.empty())
            return false;

        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        if (!restartIfSameClip && animator.RuntimeCurrentClipKey == clipKey)
            return true;

        animator.RuntimeCurrentStateName.clear();
        animator.RuntimeCurrentClipKey = clipKey;
        animator.RuntimeStateTimeSeconds = 0.0f;
        animator.RuntimePreviousStateTimeSeconds = 0.0f;
        animator.RuntimeCurrentStateDurationSeconds = 1.0f;
        animator.RuntimeInitialized = true;
        return true;
    }

    bool ScriptableEntity::SetAnimatorBool(const std::string& parameterName, bool value)
    {
        if (!HasAnimator() || parameterName.empty())
            return false;
        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        animator.SetBoolParameter(parameterName, value);
        return true;
    }

    bool ScriptableEntity::GetAnimatorBool(const std::string& parameterName, bool fallback) const
    {
        if (!HasAnimator() || parameterName.empty())
            return fallback;
        const auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        return animator.GetBoolParameter(parameterName, fallback);
    }

    bool ScriptableEntity::SetAnimatorFloat(const std::string& parameterName, float value)
    {
        if (!HasAnimator() || parameterName.empty())
            return false;
        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        animator.SetFloatParameter(parameterName, value);
        return true;
    }

    float ScriptableEntity::GetAnimatorFloat(const std::string& parameterName, float fallback) const
    {
        if (!HasAnimator() || parameterName.empty())
            return fallback;
        const auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        return animator.GetFloatParameter(parameterName, fallback);
    }

    bool ScriptableEntity::SetAnimatorInteger(const std::string& parameterName, int32_t value)
    {
        if (!HasAnimator() || parameterName.empty())
            return false;
        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        animator.SetIntegerParameter(parameterName, value);
        return true;
    }

    int32_t ScriptableEntity::GetAnimatorInteger(const std::string& parameterName, int32_t fallback) const
    {
        if (!HasAnimator() || parameterName.empty())
            return fallback;
        const auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        return animator.GetIntegerParameter(parameterName, fallback);
    }

    bool ScriptableEntity::SetAnimatorTrigger(const std::string& parameterName)
    {
        if (!HasAnimator() || parameterName.empty())
            return false;
        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        animator.SetTrigger(parameterName);
        return true;
    }

    bool ScriptableEntity::ResetAnimatorTrigger(const std::string& parameterName)
    {
        if (!HasAnimator() || parameterName.empty())
            return false;
        auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        animator.ResetTrigger(parameterName);
        return true;
    }

    std::string ScriptableEntity::GetAnimatorCurrentStateName() const
    {
        if (!HasAnimator())
            return {};
        const auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        return animator.RuntimeCurrentStateName;
    }

    float ScriptableEntity::GetAnimatorStateTimeSeconds() const
    {
        if (!HasAnimator())
            return 0.0f;
        const auto& animator = m_Registry->get<AnimatorComponent>(m_EntityHandle);
        return animator.RuntimeStateTimeSeconds;
    }

    // -------------------------------------------------------------------------
    // Particle emitter controls
    // -------------------------------------------------------------------------

    void ScriptableEntity::PlayParticles()
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter)
            return;

        ParticleEmitterPlay(*emitter);
    }

    void ScriptableEntity::StopParticles(bool clearParticles)
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter)
            return;

        ParticleEmitterStop(*emitter, clearParticles);
    }

    void ScriptableEntity::PauseParticles()
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter)
            return;

        ParticleEmitterPause(*emitter);
    }

    void ScriptableEntity::ResumeParticles()
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter)
            return;

        ParticleEmitterResume(*emitter);
    }

    void ScriptableEntity::EmitParticles(uint32_t count)
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter)
            return;

        auto* transform = m_Registry->try_get<TransformComponent>(m_EntityHandle);
        const glm::vec2 worldPos = transform
            ? glm::vec2(transform->Position.x, transform->Position.y)
            : glm::vec2(0.0f);

        ParticleEmitterEmit(*emitter, count, worldPos);
    }

    void ScriptableEntity::SetSpawnRate(float rate)
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (emitter) emitter->SpawnRate = std::max(0.0f, rate);
    }

    void ScriptableEntity::SetParticleColor(const glm::vec4& startColor, const glm::vec4& endColor)
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return;
        auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter) return;
        emitter->StartColor = startColor;
        emitter->EndColor   = endColor;
    }

    bool ScriptableEntity::IsEmitterPlaying() const
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return false;
        const auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        return emitter && emitter->Playing;
    }

    uint32_t ScriptableEntity::GetAliveParticleCount() const
    {
        if (!m_Registry || !m_Registry->valid(m_EntityHandle))
            return 0;
        const auto* emitter = m_Registry->try_get<ParticleEmitterComponent>(m_EntityHandle);
        if (!emitter || !emitter->RuntimeState) return 0;
        return emitter->RuntimeState->AliveCount;
    }

}
