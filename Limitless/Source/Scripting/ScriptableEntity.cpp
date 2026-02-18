#include "Scripting/ScriptableEntity.h"

#include "Scene/Components.h"

#ifndef SCRIPTCORE_EXPORTS
    #include "Physics/Physics2DQueries.h"
    #include "Scene/Scene.h"
#endif

namespace Limitless
{
    namespace
    {
        ScriptCreateEntityBridgeCallback s_CreateEntityBridgeCallback = nullptr;
        ScriptDestroyEntityBridgeCallback s_DestroyEntityBridgeCallback = nullptr;
        ScriptInstantiatePrefabBridgeCallback s_InstantiatePrefabBridgeCallback = nullptr;
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

    Entity ScriptableEntity::Instantiate(const ScriptPrefabReference& prefabReference, entt::entity parentEntity)
    {
        return Instantiate(prefabReference.AssetKey, parentEntity);
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

    ScriptPrefabReference ScriptableEntity::GetExposedPrefab(const std::string& name, const ScriptPrefabReference& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<ScriptPrefabReference>(&found->second))
            return *value;
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
        if (value && m_Registry && m_Registry->valid(value.GetHandle()))
        {
            if (const auto* tagComponent = m_Registry->try_get<TagComponent>(value.GetHandle()))
                entityReference.Tag = tagComponent->Tag;
        }
        (*m_ExposedProperties)[name] = std::move(entityReference);
    }

    void ScriptableEntity::SetExposedPrefab(const std::string& name, const ScriptPrefabReference& value)
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
        const Physics2DContactListener* contacts = m_Scene->GetPhysics2DContactEvents();
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
        const Physics2DContactListener* contacts = m_Scene->GetPhysics2DContactEvents();
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

}
