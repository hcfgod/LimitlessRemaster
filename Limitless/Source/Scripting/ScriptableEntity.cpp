#include "Scripting/ScriptableEntity.h"

#ifndef SCRIPTCORE_EXPORTS
    #include "Physics/Physics2DQueries.h"
    #include "Scene/Scene.h"
#endif

namespace Limitless
{
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
