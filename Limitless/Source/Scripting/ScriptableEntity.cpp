#include "Scripting/ScriptableEntity.h"

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
    }

    void ScriptableEntity::SetCreateEntityBridgeCallback(ScriptCreateEntityBridgeCallback callback)
    {
        s_CreateEntityBridgeCallback = callback;
    }

    void ScriptableEntity::SetDestroyEntityBridgeCallback(ScriptDestroyEntityBridgeCallback callback)
    {
        s_DestroyEntityBridgeCallback = callback;
    }

    bool Entity::IsValid() const
    {
        return m_ScriptOwner && m_ScriptOwner->IsEntityValid(m_EntityHandle);
    }

    void Entity::Destroy()
    {
        if (!m_ScriptOwner)
            return;
        m_ScriptOwner->DestroyEntity(m_EntityHandle);
        m_EntityHandle = entt::null;
    }

    Entity ScriptableEntity::CreateEntity(const std::string& name)
    {
        return Entity(this, CreateEntityHandle(name));
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

}
